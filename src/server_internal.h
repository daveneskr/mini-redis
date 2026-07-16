#ifndef MINI_REDIS_SERVER_INTERNAL_H
#define MINI_REDIS_SERVER_INTERNAL_H

#include "client_registry.h"
#include "server.h"
#include "store.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal server state for the future event loop.
 *
 * Ownership:
 *   - listener_fd is owned by Server when it is >= 0.
 *   - clients is owned by Server and owns every ClientSession it contains.
 *   - store is borrowed from mini_redis_run/server_run and is not destroyed by
 *     the Server object.
 */
typedef struct Server {
    int listener_fd;
    Store *store;
    ServerConfig config;
    ClientRegistry *clients;
    ServerStats stats;
} Server;

static inline size_t server_effective_max_clients(const ServerConfig *config) {
    if (config == NULL || config->max_clients == 0U) {
        return MINI_REDIS_DEFAULT_MAX_CLIENTS;
    }
    return config->max_clients;
}

#ifdef __cplusplus
}
#endif

#endif
