#ifndef COMMON_H
#define COMMON_H

/*
 * Shared types used across the whole proxy.
 *
 * The single most important idea here is `ev_tag_t`. Every fd we register
 * with epoll stores a POINTER to one of these tags in `epoll_event.data.ptr`.
 * When epoll_wait hands an event back to us, we read that pointer and instantly
 * know (a) what KIND of fd fired (listener? timer? a client side? a backend
 * side? a health probe?) and (b) which object it belongs to -- with zero
 * lookups or searching. The kernel just hands our own object back to us.
 */

typedef enum {
    EV_LISTENER,  /* the listening socket: readable => new connection to accept */
    EV_TIMER,     /* the health-check timerfd: readable => time to probe backends */
    EV_CLIENT,    /* the client side of a proxied connection */
    EV_BACKEND,   /* the backend side of a proxied connection */
    EV_PROBE      /* a temporary health-probe socket */
} ev_type_t;

typedef struct ev_tag {
    ev_type_t type;
    void     *owner;  /* connection_t* for CLIENT/BACKEND, backend_t* for PROBE, NULL otherwise */
} ev_tag_t;

/* Put a socket into non-blocking mode. Returns 0 on success, -1 on error. */
int make_nonblocking(int fd);

#endif /* COMMON_H */
