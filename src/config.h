#ifndef CONFIG_H
#define CONFIG_H

#include "backend.h"

/*
 * Compile-time configuration. Kept deliberately simple: edit these and
 * rebuild. (A real proxy would read a config file / CLI flags -- a good
 * "next feature" to mention in an interview.)
 *
 * This header is included by exactly one .c file (main.c), so the static
 * BACKENDS array below does not cause multiple-definition problems.
 */

#define LISTEN_PORT          8080
#define LB_STRATEGY          LB_LEAST_CONN   /* or LB_ROUND_ROBIN */
#define HEALTH_INTERVAL_SEC  3

static const struct {
    const char *host;
    int         port;
} BACKENDS[] = {
    { "127.0.0.1", 9001 },
    { "127.0.0.1", 9002 },
    { "127.0.0.1", 9003 },
};

#define NUM_BACKENDS ((int)(sizeof(BACKENDS) / sizeof(BACKENDS[0])))

#endif /* CONFIG_H */
