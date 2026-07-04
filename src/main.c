#include <stdio.h>
#include <signal.h>

#include "config.h"
#include "backend.h"
#include "loop.h"

int main(void)
{
    /*
     * Writing to a socket whose peer has closed raises SIGPIPE, which by
     * default kills the process. We ignore it and instead detect the closed
     * peer via write() returning -1 with errno == EPIPE inside pump().
     */
    signal(SIGPIPE, SIG_IGN);

    backend_pool_t pool;
    if (backend_pool_init(&pool, LB_STRATEGY) != 0) {
        fprintf(stderr, "failed to init backend pool\n");
        return 1;
    }

    for (int i = 0; i < NUM_BACKENDS; i++) {
        if (backend_pool_add(&pool, BACKENDS[i].host, BACKENDS[i].port) != 0) {
            fprintf(stderr, "failed to add backend %s:%d\n",
                    BACKENDS[i].host, BACKENDS[i].port);
            return 1;
        }
    }

    return loop_run(&pool, LISTEN_PORT, HEALTH_INTERVAL_SEC);
}
