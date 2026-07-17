#include "client_session.h"

#include <stdlib.h>
#include <string.h>

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

bool client_session_consume_input(ClientSession *client, size_t consumed) {
    if (client == NULL || consumed == 0U || consumed > client->input_length) {
        return false;
    }

    const size_t remaining = client->input_length - consumed;
    if (remaining > 0U) {
        memmove(client->input_buffer,
                client->input_buffer + consumed,
                remaining);
    }
    client->input_length = remaining;
    return true;
}

void client_session_reset_output(ClientSession *client) {
    if (client == NULL) {
        return;
    }

    client->output_length = 0U;
    client->output_offset = 0U;
}

bool client_session_ready_to_close(const ClientSession *client) {
    return client != NULL &&
           client->close_requested &&
           !client_session_has_pending_output(client);
}
