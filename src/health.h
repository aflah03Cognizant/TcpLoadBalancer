#ifndef HEALTH_H
#define HEALTH_H

#include <stdint.h>
#include "backend.h"

/*
 * Health checking, done entirely inside the single event loop -- no extra
 * threads. A timerfd fires every N seconds; on each tick we open a throwaway
 * non-blocking socket per backend and try to connect(). If the connect
 * succeeds we mark the backend UP, otherwise DOWN. Because the probe sockets
 * live in the same epoll set as everything else, nothing ever blocks.
 */

/* Create and arm a periodic timerfd. Returns the fd (register it with epoll). */
int health_timer_create(int interval_sec);

/* Called when the timerfd becomes readable: fire a fresh round of probes. */
void health_on_tick(int epfd, backend_pool_t *pool, int timerfd);

/* Called when a probe socket (EV_PROBE) becomes ready: record the result. */
void health_on_probe_event(int epfd, backend_t *b, uint32_t revents);

#endif /* HEALTH_H */
