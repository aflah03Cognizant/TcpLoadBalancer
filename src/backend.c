#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

#include "backend.h"

int backend_pool_init(backend_pool_t *pool, lb_strategy_t strategy)
{
    pool->items = calloc(MAX_BACKENDS, sizeof(backend_t));
    if (!pool->items)
        return -1;
    pool->count    = 0;
    pool->strategy = strategy;
    pool->rr_next  = 0;
    return 0;
}

int backend_pool_add(backend_pool_t *pool, const char *host, int port)
{
    if (pool->count >= MAX_BACKENDS)
        return -1;

    backend_t *b = &pool->items[pool->count];
    memset(b, 0, sizeof(*b));

    strncpy(b->host, host, sizeof(b->host) - 1);
    b->port     = port;
    b->probe_fd = -1;
    /* Start optimistic: route immediately, let the first health probe demote
     * a backend if it's actually down. */
    b->status   = BACKEND_HEALTHY;

    b->addr.sin_family = AF_INET;
    b->addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &b->addr.sin_addr) != 1) {
        fprintf(stderr, "backend_pool_add: bad IPv4 address '%s'\n", host);
        return -1;
    }

    pool->count++;
    return 0;
}

backend_t *backend_pick(backend_pool_t *pool)
{
    if (pool->count == 0)
        return NULL;

    if (pool->strategy == LB_ROUND_ROBIN) {
        /* Walk forward from the cursor, skipping unhealthy backends. */
        for (int i = 0; i < pool->count; i++) {
            int idx = (pool->rr_next + i) % pool->count;
            if (pool->items[idx].status == BACKEND_HEALTHY) {
                pool->rr_next = (idx + 1) % pool->count;
                return &pool->items[idx];
            }
        }
        return NULL; /* nobody healthy */
    }

    /* LB_LEAST_CONN: pick the healthy backend with the fewest active conns. */
    backend_t *best = NULL;
    for (int i = 0; i < pool->count; i++) {
        backend_t *b = &pool->items[i];
        if (b->status != BACKEND_HEALTHY)
            continue;
        if (!best || b->active_connections < best->active_connections)
            best = b;
    }
    return best;
}

void backend_set_status(backend_t *b, backend_status_t status)
{
    if (b->status != status) {
        fprintf(stderr, "[health] %s:%d -> %s\n",
                b->host, b->port,
                status == BACKEND_HEALTHY ? "UP" : "DOWN");
    }
    b->status = status;
}
