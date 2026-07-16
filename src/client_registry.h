#ifndef MINI_REDIS_CLIENT_REGISTRY_H
#define MINI_REDIS_CLIENT_REGISTRY_H

#include "client_session.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ClientRegistryResult {
    CLIENT_REGISTRY_OK = 0,
    CLIENT_REGISTRY_INVALID_ARGUMENT,
    CLIENT_REGISTRY_OUT_OF_MEMORY,
    CLIENT_REGISTRY_LIMIT_REACHED,
    CLIENT_REGISTRY_DUPLICATE_DESCRIPTOR,
    CLIENT_REGISTRY_NOT_FOUND
} ClientRegistryResult;

/**
 * Bounded owner of active client sessions.
 *
 * Ownership:
 *   - client_registry_add takes ownership of a ClientSession only on
 *     CLIENT_REGISTRY_OK.
 *   - client_registry_remove_at destroys the removed ClientSession.
 *   - client_registry_destroy destroys every remaining ClientSession.
 *
 * Removal uses swap-with-last. Stable client ordering is intentionally not
 * guaranteed; future poll-loop code should rebuild readiness arrays from the
 * current registry state each iteration or handle index changes explicitly.
 */
typedef struct ClientRegistry {
    ClientSession **clients;
    size_t count;
    size_t capacity;
} ClientRegistry;

ClientRegistry *client_registry_create(size_t capacity);
void client_registry_destroy(ClientRegistry *registry);

ClientRegistryResult client_registry_add(ClientRegistry *registry,
                                         ClientSession *client);
ClientRegistryResult client_registry_remove_at(ClientRegistry *registry,
                                               size_t index);
ClientRegistryResult client_registry_remove_by_fd(ClientRegistry *registry,
                                                  int socket_fd);

ClientSession *client_registry_get(const ClientRegistry *registry,
                                   size_t index);
ClientSession *client_registry_find_by_fd(const ClientRegistry *registry,
                                          int socket_fd);

size_t client_registry_count(const ClientRegistry *registry);
size_t client_registry_capacity(const ClientRegistry *registry);

const char *client_registry_result_string(ClientRegistryResult result);

#ifdef __cplusplus
}
#endif

#endif
