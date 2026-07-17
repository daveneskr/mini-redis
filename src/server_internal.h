#ifndef MINI_REDIS_SERVER_INTERNAL_H
#define MINI_REDIS_SERVER_INTERNAL_H

#include "client_registry.h"
#include "server.h"
#include "store.h"

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal event-loop state.
 *
 * Ownership:
 *   - listener_fd is owned by Server when it is >= 0;
 *   - clients is owned by Server and owns every ClientSession it contains;
 *   - poll_fds is owned by Server;
 *   - store and config.bind_address are borrowed.
 */
typedef struct Server {
    int listener_fd;
    Store *store;
    ServerConfig config;
    ClientRegistry *clients;
    ServerStats stats;

    struct pollfd *poll_fds;
    size_t poll_capacity;

    bool accepting_clients;
    bool accepted_once;
} Server;

static inline size_t server_effective_max_clients(const ServerConfig *config) {
    if (config == NULL || config->max_clients == 0U) {
        return MINI_REDIS_DEFAULT_MAX_CLIENTS;
    }
    return config->max_clients;
}

ServerResult server_initialize(Server *server,
                               const ServerConfig *config,
                               Store *store,
                               uint16_t *bound_port_out);
ServerResult server_event_loop(Server *server);
void server_destroy(Server *server);

#ifdef __cplusplus
}
#endif

#endif
