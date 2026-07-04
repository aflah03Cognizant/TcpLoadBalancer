#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "health.h"
#include "common.h"

int health_timer_create(int interval_sec)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd < 0)
        return -1;

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec    = interval_sec;   /* first fire */
    its.it_interval.tv_sec = interval_sec;   /* then every interval */

    if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
        close(tfd);
        return -1;
    }
    return tfd;
}

/* Tear down an in-flight probe socket and clear the probe state. */
static void cleanup_probe(int epfd, backend_t *b)
{
    if (!b->probing)
        return;
    epoll_ctl(epfd, EPOLL_CTL_DEL, b->probe_fd, NULL);
    close(b->probe_fd);
    b->probe_fd = -1;
    b->probing  = 0;
}

/* Kick off one non-blocking connect() probe against a backend. */
static void start_probe(int epfd, backend_t *b)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        backend_set_status(b, BACKEND_UNHEALTHY);
        return;
    }
    make_nonblocking(fd);

    int r = connect(fd, (struct sockaddr *)&b->addr, sizeof(b->addr));
    if (r == 0) {
        /* Connected instantly (common for loopback) -- healthy, done. */
        backend_set_status(b, BACKEND_HEALTHY);
        close(fd);
        return;
    }
    if (errno != EINPROGRESS) {
        /* Immediate failure, e.g. nothing listening. */
        backend_set_status(b, BACKEND_UNHEALTHY);
        close(fd);
        return;
    }

    /* Connect is in flight: epoll will tell us via EPOLLOUT when it resolves. */
    b->probe_fd        = fd;
    b->probing         = 1;
    b->probe_tag.type  = EV_PROBE;
    b->probe_tag.owner = b;

    struct epoll_event ev;
    ev.events   = EPOLLOUT;
    ev.data.ptr = &b->probe_tag;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

void health_on_tick(int epfd, backend_pool_t *pool, int timerfd)
{
    uint64_t expirations;
    (void)!read(timerfd, &expirations, sizeof(expirations)); /* drain the timer */

    for (int i = 0; i < pool->count; i++) {
        backend_t *b = &pool->items[i];

        /* If last round's probe never completed, treat the backend as down
         * (the connect didn't finish within one interval). */
        if (b->probing) {
            cleanup_probe(epfd, b);
            backend_set_status(b, BACKEND_UNHEALTHY);
        }

        start_probe(epfd, b);
    }
}

void health_on_probe_event(int epfd, backend_t *b, uint32_t revents)
{
    int healthy = 0;

    if (!(revents & (EPOLLERR | EPOLLHUP))) {
        /* Ask the socket whether the connect actually succeeded. */
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(b->probe_fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)
            healthy = 1;
    }

    backend_set_status(b, healthy ? BACKEND_HEALTHY : BACKEND_UNHEALTHY);
    cleanup_probe(epfd, b);
}
