#include <stdlib.h>
#include <string.h>

#include "connection.h"

connection_t *connection_create(int client_fd, int backend_fd, backend_t *b)
{
    connection_t *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->client_fd  = client_fd;
    c->backend_fd = backend_fd;
    c->backend    = b;

    /* Wire up the tags so epoll can hand us back exactly which side fired. */
    c->client_tag.type   = EV_CLIENT;
    c->client_tag.owner  = c;
    c->backend_tag.type  = EV_BACKEND;
    c->backend_tag.owner = c;

    return c;
}
