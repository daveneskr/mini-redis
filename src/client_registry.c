#include "client_registry.h"

#include <stdint.h>
#include <stdlib.h>

static int registry_find_index_by_fd(const ClientRegistry *registry,
                                     int socket_fd,
                                     size_t *index_out) {
    if (registry == NULL || socket_fd < 0) {
        return 0;
    }

    for (size_t i = 0U; i < registry->count; ++i) {
        const ClientSession *client = registry->clients[i];
        if (client != NULL && client_session_socket_fd(client) == socket_fd) {
            if (index_out != NULL) {
                *index_out = i;
            }
            return 1;
        }
    }

    return 0;
}

ClientRegistry *client_registry_create(size_t capacity) {
    if (capacity == 0U || capacity > SIZE_MAX / sizeof(ClientSession *)) {
        return NULL;
    }

    ClientRegistry *registry = calloc(1U, sizeof(*registry));
    if (registry == NULL) {
        return NULL;
    }

    registry->clients = calloc(capacity, sizeof(*registry->clients));
    if (registry->clients == NULL) {
        free(registry);
        return NULL;
    }

    registry->capacity = capacity;
    return registry;
}

void client_registry_destroy(ClientRegistry *registry) {
    if (registry == NULL) {
        return;
    }

    for (size_t i = 0U; i < registry->count; ++i) {
        client_session_destroy(registry->clients[i]);
        registry->clients[i] = NULL;
    }

    free(registry->clients);
    free(registry);
}

ClientRegistryResult client_registry_add(ClientRegistry *registry,
                                         ClientSession *client) {
    if (registry == NULL || client == NULL || registry->clients == NULL) {
        return CLIENT_REGISTRY_INVALID_ARGUMENT;
    }

    const int socket_fd = client_session_socket_fd(client);
    if (socket_fd < 0) {
        return CLIENT_REGISTRY_INVALID_ARGUMENT;
    }

    if (registry_find_index_by_fd(registry, socket_fd, NULL)) {
        return CLIENT_REGISTRY_DUPLICATE_DESCRIPTOR;
    }

    if (registry->count >= registry->capacity) {
        return CLIENT_REGISTRY_LIMIT_REACHED;
    }

    registry->clients[registry->count] = client;
    ++registry->count;
    return CLIENT_REGISTRY_OK;
}

ClientRegistryResult client_registry_remove_at(ClientRegistry *registry,
                                               size_t index) {
    if (registry == NULL || registry->clients == NULL) {
        return CLIENT_REGISTRY_INVALID_ARGUMENT;
    }
    if (index >= registry->count) {
        return CLIENT_REGISTRY_NOT_FOUND;
    }

    client_session_destroy(registry->clients[index]);

    const size_t last_index = registry->count - 1U;
    if (index != last_index) {
        registry->clients[index] = registry->clients[last_index];
    }
    registry->clients[last_index] = NULL;
    --registry->count;
    return CLIENT_REGISTRY_OK;
}

ClientRegistryResult client_registry_remove_by_fd(ClientRegistry *registry,
                                                  int socket_fd) {
    if (registry == NULL || registry->clients == NULL || socket_fd < 0) {
        return CLIENT_REGISTRY_INVALID_ARGUMENT;
    }

    size_t index = 0U;
    if (!registry_find_index_by_fd(registry, socket_fd, &index)) {
        return CLIENT_REGISTRY_NOT_FOUND;
    }

    return client_registry_remove_at(registry, index);
}

ClientSession *client_registry_get(const ClientRegistry *registry,
                                   size_t index) {
    if (registry == NULL || registry->clients == NULL || index >= registry->count) {
        return NULL;
    }
    return registry->clients[index];
}

ClientSession *client_registry_find_by_fd(const ClientRegistry *registry,
                                          int socket_fd) {
    size_t index = 0U;
    if (!registry_find_index_by_fd(registry, socket_fd, &index)) {
        return NULL;
    }
    return registry->clients[index];
}

size_t client_registry_count(const ClientRegistry *registry) {
    return registry == NULL ? 0U : registry->count;
}

size_t client_registry_capacity(const ClientRegistry *registry) {
    return registry == NULL ? 0U : registry->capacity;
}

const char *client_registry_result_string(ClientRegistryResult result) {
    switch (result) {
        case CLIENT_REGISTRY_OK: return "CLIENT_REGISTRY_OK";
        case CLIENT_REGISTRY_INVALID_ARGUMENT: return "CLIENT_REGISTRY_INVALID_ARGUMENT";
        case CLIENT_REGISTRY_OUT_OF_MEMORY: return "CLIENT_REGISTRY_OUT_OF_MEMORY";
        case CLIENT_REGISTRY_LIMIT_REACHED: return "CLIENT_REGISTRY_LIMIT_REACHED";
        case CLIENT_REGISTRY_DUPLICATE_DESCRIPTOR: return "CLIENT_REGISTRY_DUPLICATE_DESCRIPTOR";
        case CLIENT_REGISTRY_NOT_FOUND: return "CLIENT_REGISTRY_NOT_FOUND";
        default: return "CLIENT_REGISTRY_UNKNOWN_RESULT";
    }
}
