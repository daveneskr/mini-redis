#include "client_session.h"

#include <stdlib.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

static void close_socket_fd(int socket_fd) {
    if (socket_fd < 0) {
        return;
    }

#if defined(_WIN32)
    (void) closesocket((SOCKET) socket_fd);
#else
    (void) close(socket_fd);
#endif
}

ClientSession *client_session_create(int socket_fd) {
    if (socket_fd < 0) {
        return NULL;
    }

    ClientSession *client = calloc(1U, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }

    client->socket_fd = socket_fd;
    return client;
}

void client_session_destroy(ClientSession *client) {
    if (client == NULL) {
        return;
    }

    close_socket_fd(client->socket_fd);
    client->socket_fd = -1;
    free(client);
}

int client_session_socket_fd(const ClientSession *client) {
    return client == NULL ? -1 : client->socket_fd;
}

bool client_session_has_pending_output(const ClientSession *client) {
    return client != NULL && client->output_offset < client->output_length;
}

bool client_session_input_is_full(const ClientSession *client) {
    return client != NULL && client->input_length >= sizeof(client->input_buffer);
}
