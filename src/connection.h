#ifndef CONNECTION_H
#define CONNECTION_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "backend.h"

#define BUF_SIZE 65536

/*
 * A one-directional byte pipe with a fixed buffer.
 *
 * We only refill the buffer when it is completely empty, and we drain it via
 * write() as the destination accepts bytes. `off` tracks how much of the
 * current fill has already been written out; [off, len) is still pending.
 * When off == len the buffer is empty and we reset both to 0.
 */
typedef struct buffer {
    char   data[BUF_SIZE];
    size_t len;   /* total valid bytes in this fill */
    size_t off;   /* bytes already written to the destination */
} buffer_t;

static inline size_t buffer_pending(const buffer_t *b) { return b->len - b->off; }
static inline int    buffer_empty  (const buffer_t *b) { return b->len == b->off; }

/*
 * One proxied connection = a client socket bridged to a backend socket.
 * Two independent byte pipes run over it:
 *    c2b: client -> backend   (the request stream)
 *    b2c: backend -> client   (the response stream)
 */
typedef struct connection connection_t;
struct connection {
    int        client_fd;
    int        backend_fd;
    backend_t *backend;      /* so we can decrement active_connections on close */

    ev_tag_t   client_tag;   /* {EV_CLIENT, this}  -- stored in epoll for client_fd  */
    ev_tag_t   backend_tag;  /* {EV_BACKEND, this} -- stored in epoll for backend_fd */

    buffer_t   c2b;          /* client  -> backend */
    buffer_t   b2c;          /* backend -> client  */

    /* The epoll event mask we currently have registered for each fd. We track
     * it so we only issue EPOLL_CTL_MOD when the desired mask actually changes. */
    uint32_t   client_events;
    uint32_t   backend_events;

    int connecting;   /* 1 while the non-blocking connect() to the backend is in flight */
    int client_eof;   /* client closed its write side (we saw EOF reading from it) */
    int backend_eof;  /* backend closed its write side */
    int closed;       /* marked for deferred free (see the event loop) */

    connection_t *next_free;  /* intrusive free-list link, used within one epoll batch */
};

connection_t *connection_create(int client_fd, int backend_fd, backend_t *b);

#endif /* CONNECTION_H */
