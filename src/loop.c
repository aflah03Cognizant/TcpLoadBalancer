#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "loop.h"
#include "common.h"
#include "connection.h"
#include "health.h"

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0  /* fall back gracefully if the header predates it */
#endif

#define MAX_EVENTS 256

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int setup_listener(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    if (make_nonblocking(fd) < 0) {
        perror("make_nonblocking(listener)");
        close(fd);
        return -1;
    }
    return fd;
}

/* ------------------------------------------------------------------ */
/* epoll mask computation                                              */
/*                                                                     */
/* The desired interest set for each fd is a pure function of the      */
/* connection's buffer state. This is the whole trick to correct       */
/* backpressure: we only ask to READ from a source when its outgoing   */
/* buffer is empty, and only ask to WRITE to a destination when there  */
/* is pending data for it. That way a slow backend can't make us spin, */
/* and a fast client can't overrun our fixed buffer.                   */
/* ------------------------------------------------------------------ */

static uint32_t compute_client_mask(const connection_t *c)
{
    if (c->connecting)
        return 0; /* backend not up yet: ignore the client (errors still reported) */

    uint32_t m = EPOLLRDHUP;
    if (!c->client_eof && buffer_empty(&c->c2b))
        m |= EPOLLIN;                       /* room to read the request stream */
    if (buffer_pending(&c->b2c) > 0)
        m |= EPOLLOUT;                      /* response bytes waiting to go to client */
    return m;
}

static uint32_t compute_backend_mask(const connection_t *c)
{
    if (c->connecting)
        return EPOLLOUT; /* writable => the non-blocking connect() has resolved */

    uint32_t m = EPOLLRDHUP;
    if (!c->backend_eof && buffer_empty(&c->b2c))
        m |= EPOLLIN;                       /* room to read the response stream */
    if (buffer_pending(&c->c2b) > 0)
        m |= EPOLLOUT;                      /* request bytes waiting to go to backend */
    return m;
}

static void conn_register(int epfd, connection_t *c)
{
    struct epoll_event ev;

    c->client_events   = compute_client_mask(c);
    ev.events          = c->client_events;
    ev.data.ptr        = &c->client_tag;
    epoll_ctl(epfd, EPOLL_CTL_ADD, c->client_fd, &ev);

    c->backend_events  = compute_backend_mask(c);
    ev.events          = c->backend_events;
    ev.data.ptr        = &c->backend_tag;
    epoll_ctl(epfd, EPOLL_CTL_ADD, c->backend_fd, &ev);
}

/* Re-issue EPOLL_CTL_MOD only for fds whose desired mask actually changed. */
static void conn_update(int epfd, connection_t *c)
{
    uint32_t cm = compute_client_mask(c);
    if (cm != c->client_events) {
        struct epoll_event ev = { .events = cm, .data.ptr = &c->client_tag };
        epoll_ctl(epfd, EPOLL_CTL_MOD, c->client_fd, &ev);
        c->client_events = cm;
    }

    uint32_t bm = compute_backend_mask(c);
    if (bm != c->backend_events) {
        struct epoll_event ev = { .events = bm, .data.ptr = &c->backend_tag };
        epoll_ctl(epfd, EPOLL_CTL_MOD, c->backend_fd, &ev);
        c->backend_events = bm;
    }
}

/* ------------------------------------------------------------------ */
/* Data pumping                                                        */
/* ------------------------------------------------------------------ */

/*
 * Move bytes source -> destination through a buffer. Both directions share
 * this shape; only the fds and the eof flag differ. Returns 0 normally, or
 * -1 if the connection hit a hard error and should be closed.
 *
 * `src_eof` is a pointer so we can record that the source reached EOF, and
 * once its buffer has fully drained we half-close the destination's write
 * side (shutdown SHUT_WR) so the peer sees the end of stream.
 */
static int pump(int src_fd, int dst_fd, buffer_t *b, int *src_eof)
{
    /* 1. Refill from the source, but only when the buffer is empty. */
    if (!*src_eof && buffer_empty(b)) {
        ssize_t n = read(src_fd, b->data, BUF_SIZE);
        if (n > 0) {
            b->len = (size_t)n;
            b->off = 0;
        } else if (n == 0) {
            *src_eof = 1;                   /* peer closed its write side */
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return -1;                      /* real read error */
        }
        /* EAGAIN/EWOULDBLOCK: nothing available right now -- fine. */
    }

    /* 2. Drain whatever is pending toward the destination. */
    while (buffer_pending(b) > 0) {
        ssize_t n = write(dst_fd, b->data + b->off, buffer_pending(b));
        if (n > 0) {
            b->off += (size_t)n;
            if (b->off == b->len) {         /* fully drained: reset to empty */
                b->len = 0;
                b->off = 0;
            }
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;                          /* dest send buffer full: try later on EPOLLOUT */
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;                      /* real write error (e.g. EPIPE) */
        }
    }

    /* 3. If the source is done and we've flushed everything, tell the dest. */
    if (*src_eof && buffer_empty(b))
        shutdown(dst_fd, SHUT_WR);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Connection lifecycle                                                */
/* ------------------------------------------------------------------ */

/*
 * Close a connection. We DEFER the free: within a single epoll_wait batch
 * both the client side and backend side of the same connection may appear,
 * so freeing immediately would leave a dangling pointer for the second event.
 * Instead we mark it closed, unregister + close the fds now (so no future
 * events), and push it onto a free list drained after the batch.
 */
static void conn_close(int epfd, connection_t *c, connection_t **free_head)
{
    if (c->closed)
        return;
    c->closed = 1;

    epoll_ctl(epfd, EPOLL_CTL_DEL, c->client_fd,  NULL);
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->backend_fd, NULL);
    close(c->client_fd);
    close(c->backend_fd);

    if (c->backend && c->backend->active_connections > 0)
        c->backend->active_connections--;

    c->next_free = *free_head;
    *free_head   = c;
}

static void accept_connections(int epfd, int listen_fd, backend_pool_t *pool)
{
    /* Level-triggered: one accept() per event would also work, but draining
     * in a loop handles bursts and is the edge-triggered-safe habit. */
    for (;;) {
        int cfd = accept(listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;                      /* no more pending connections */
            if (errno == EINTR)
                continue;
            break;                          /* transient error: stop this round */
        }
        make_nonblocking(cfd);

        backend_t *b = backend_pick(pool);
        if (!b) {
            /* No healthy backend to serve this client. */
            close(cfd);
            continue;
        }

        int bfd = socket(AF_INET, SOCK_STREAM, 0);
        if (bfd < 0) {
            close(cfd);
            continue;
        }
        make_nonblocking(bfd);

        int r = connect(bfd, (struct sockaddr *)&b->addr, sizeof(b->addr));
        if (r < 0 && errno != EINPROGRESS) {
            close(bfd);
            close(cfd);
            continue;
        }

        connection_t *c = connection_create(cfd, bfd, b);
        if (!c) {
            close(bfd);
            close(cfd);
            continue;
        }
        c->connecting = (r < 0);            /* EINPROGRESS => still connecting */
        b->active_connections++;

        conn_register(epfd, c);
    }
}

/* Handle any event on either side of a live connection. */
static void handle_conn_event(int epfd, connection_t *c, ev_type_t which,
                              uint32_t revents, connection_t **free_head)
{
    if (revents & EPOLLERR) {
        conn_close(epfd, c, free_head);
        return;
    }

    /* Non-blocking connect resolution: the backend fd becomes writable (or
     * errors) when connect() finishes. Confirm success via SO_ERROR. */
    if (c->connecting) {
        if (which == EV_BACKEND && (revents & (EPOLLOUT | EPOLLHUP))) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(c->backend_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                conn_close(epfd, c, free_head);
                return;
            }
            c->connecting = 0;              /* connected! fall through to pump */
        } else {
            return;                         /* still connecting; ignore for now */
        }
    }

    /* Pump both directions opportunistically. Each pump is guarded by its own
     * buffer state, so calling both on any event is cheap and always correct. */
    if (pump(c->client_fd, c->backend_fd, &c->c2b, &c->client_eof) < 0) {
        conn_close(epfd, c, free_head);
        return;
    }
    if (pump(c->backend_fd, c->client_fd, &c->b2c, &c->backend_eof) < 0) {
        conn_close(epfd, c, free_head);
        return;
    }

    /* Both halves finished and everything is flushed => we're done. */
    if (c->client_eof && c->backend_eof &&
        buffer_empty(&c->c2b) && buffer_empty(&c->b2c)) {
        conn_close(epfd, c, free_head);
        return;
    }

    conn_update(epfd, c);
}

/* ------------------------------------------------------------------ */
/* The event loop                                                      */
/* ------------------------------------------------------------------ */

int loop_run(backend_pool_t *pool, int listen_port, int health_interval_sec)
{
    int listen_fd = setup_listener(listen_port);
    if (listen_fd < 0)
        return 1;

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        return 1;
    }

    /* Static tags for the two "singleton" fds. Their .owner is unused. */
    static ev_tag_t listener_tag = { EV_LISTENER, NULL };
    static ev_tag_t timer_tag    = { EV_TIMER,    NULL };

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.ptr = &listener_tag;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    int timerfd = health_timer_create(health_interval_sec);
    if (timerfd < 0) {
        perror("health_timer_create");
        close(epfd);
        close(listen_fd);
        return 1;
    }
    ev.events   = EPOLLIN;
    ev.data.ptr = &timer_tag;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timerfd, &ev);

    fprintf(stderr, "tcplb: listening on :%d, %d backend(s), health every %ds\n",
            listen_port, pool->count, health_interval_sec);

    struct epoll_event events[MAX_EVENTS];

    for (;;) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        connection_t *free_head = NULL;     /* deferred frees for this batch */

        for (int i = 0; i < n; i++) {
            ev_tag_t *tag     = events[i].data.ptr;
            uint32_t  revents = events[i].events;

            switch (tag->type) {
            case EV_LISTENER:
                accept_connections(epfd, listen_fd, pool);
                break;

            case EV_TIMER:
                health_on_tick(epfd, pool, timerfd);
                break;

            case EV_PROBE:
                health_on_probe_event(epfd, (backend_t *)tag->owner, revents);
                break;

            case EV_CLIENT:
            case EV_BACKEND: {
                connection_t *c = (connection_t *)tag->owner;
                if (c->closed)
                    break;                  /* stale event for an already-closed conn */
                handle_conn_event(epfd, c, tag->type, revents, &free_head);
                break;
            }
            }
        }

        /* Safe to free now that the whole batch is processed. */
        while (free_head) {
            connection_t *next = free_head->next_free;
            free(free_head);
            free_head = next;
        }
    }

    close(timerfd);
    close(epfd);
    close(listen_fd);
    return 0;
}
