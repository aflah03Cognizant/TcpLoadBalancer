#ifndef BACKEND_H
#define BACKEND_H

#include <netinet/in.h>
#include "common.h"

#define MAX_BACKENDS 32

/* Load-balancing strategy. */
typedef enum {
    LB_ROUND_ROBIN,   /* hand out backends in rotation */
    LB_LEAST_CONN     /* pick the healthy backend with the fewest active connections */
} lb_strategy_t;

/* Whether we currently believe a backend is usable. */
typedef enum {
    BACKEND_HEALTHY,
    BACKEND_UNHEALTHY
} backend_status_t;

typedef struct backend {
    char host[64];
    int  port;
    struct sockaddr_in addr;      /* resolved once at startup, reused for every connect */

    backend_status_t status;
    int active_connections;       /* live proxied connections using this backend (for least-conn) */

    /* Health-probe state (see health.c). A probe is a throwaway socket that
     * tries to TCP-connect to the backend; success => healthy. */
    int      probe_fd;            /* -1 when no probe in flight */
    int      probing;             /* 1 while a probe connect is outstanding */
    ev_tag_t probe_tag;           /* {EV_PROBE, this} handed to epoll for the probe fd */
} backend_t;

typedef struct backend_pool {
    backend_t    *items;
    int           count;
    lb_strategy_t strategy;
    int           rr_next;        /* cursor for round-robin */
} backend_pool_t;

int  backend_pool_init(backend_pool_t *pool, lb_strategy_t strategy);
int  backend_pool_add (backend_pool_t *pool, const char *host, int port);

/* Choose a HEALTHY backend per the pool's strategy. Returns NULL if none are up. */
backend_t *backend_pick(backend_pool_t *pool);

/* Set status and log a line when it actually changes (nice for the demo). */
void backend_set_status(backend_t *b, backend_status_t status);

#endif /* BACKEND_H */
