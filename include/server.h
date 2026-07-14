#ifndef MINI_REDIS_SERVER_H
#define MINI_REDIS_SERVER_H

#include "store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINI_REDIS_DEFAULT_BIND_ADDRESS "127.0.0.1"
#define MINI_REDIS_DEFAULT_PORT 6380U
#define MINI_REDIS_DEFAULT_BACKLOG 8

typedef struct ServerConfig {
    const char *bind_address;
    uint16_t port;
    int listen_backlog;

    /**
     * When false, server_run returns after the first client session. This is
     * useful for deterministic tests. Normal server operation should use true.
     */
    bool continue_after_client_disconnect;
} ServerConfig;

typedef enum ServerIoResult {
    SERVER_IO_OK = 0,
    SERVER_IO_INVALID_ARGUMENT,
    SERVER_IO_DISCONNECTED,
    SERVER_IO_ERROR
} ServerIoResult;

typedef enum ServerResult {
    SERVER_OK = 0,
    SERVER_STOPPED,
    SERVER_INVALID_ARGUMENT,
    SERVER_SOCKET_ERROR,
    SERVER_SOCKET_OPTION_ERROR,
    SERVER_ADDRESS_ERROR,
    SERVER_BIND_ERROR,
    SERVER_LISTEN_ERROR,
    SERVER_SIGNAL_ERROR,
    SERVER_ACCEPT_ERROR,
    SERVER_CLIENT_IO_ERROR,
    SERVER_CLIENT_PROTOCOL_ERROR,
    SERVER_INTERNAL_ERROR
} ServerResult;

/**
 * Runs a blocking IPv4 TCP server and processes one active client at a time.
 * The caller owns store; server_run never destroys it.
 */
ServerResult server_run(const ServerConfig *config, Store *store);

/**
 * Processes one already-connected client until disconnect or a fatal client
 * error. This function takes ownership of client_fd and closes it exactly once.
 */
ServerResult server_handle_client(int client_fd, Store *store);

/** Sends exactly length bytes unless the peer disconnects or an error occurs. */
ServerIoResult server_send_all(int socket_fd,
                               const unsigned char *buffer,
                               size_t length);

/** Requests a running server loop to stop at its next interruption point. */
void server_request_stop(void);

const char *server_result_string(ServerResult result);
const char *server_io_result_string(ServerIoResult result);

#ifdef __cplusplus
}
#endif

#endif
