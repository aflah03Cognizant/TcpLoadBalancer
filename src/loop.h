#ifndef LOOP_H
#define LOOP_H

#include "backend.h"

/*
 * Run the proxy: bind a listener on `listen_port`, set up health checking,
 * and drive the single-threaded epoll event loop forever.
 * Returns non-zero only on a fatal setup error.
 */
int loop_run(backend_pool_t *pool, int listen_port, int health_interval_sec);

#endif /* LOOP_H */
