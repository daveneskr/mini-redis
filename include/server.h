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
#define MINI_REDIS_DEFAULT_BACKLOG 16
#define MINI_REDIS_DEFAULT_MAX_CLIENTS 64U
#define MINI_REDIS_POLL_TIMEOUT_MS 100
#define MINI_REDIS_MAX_ACCEPTS_PER_TICK 16U
#define MINI_REDIS_MAX_READS_PER_TICK 4U
#define MINI_REDIS_MAX_WRITES_PER_TICK 4U

typedef struct ServerStats {
    unsigned long long accepted_connections;
    unsigned long long active_clients;
    unsigned long long peak_clients;
    unsigned long long completed_commands;
    unsigned long long protocol_errors;
    unsigned long long rejected_connections;
    unsigned long long receive_errors;
    unsigned long long send_errors;
} ServerStats;

typedef struct ServerConfig {
    const char *bind_address;
    uint16_t port;
    int listen_backlog;

    /** A value of 0 selects MINI_REDIS_DEFAULT_MAX_CLIENTS. */
    size_t max_clients;

    /**
     * When false, accept exactly one client, stop accepting further clients,
     * and return after that client's buffered input/output has been drained.
     */
    bool continue_after_client_disconnect;
} ServerConfig;

typedef enum ServerIoResult {
    SERVER_IO_OK = 0,
    SERVER_IO_WOULD_BLOCK,
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
    SERVER_NONBLOCKING_ERROR,
    SERVER_ADDRESS_ERROR,
    SERVER_BIND_ERROR,
    SERVER_LISTEN_ERROR,
    SERVER_SIGNAL_ERROR,
    SERVER_ACCEPT_ERROR,
    SERVER_POLL_ERROR,
    SERVER_CLIENT_IO_ERROR,
    SERVER_CLIENT_PROTOCOL_ERROR,
    SERVER_INTERNAL_ERROR
} ServerResult;

/**
 * Runs the single-threaded, non-blocking poll-based server.
 * The caller owns store; server_run never destroys it.
 */
ServerResult server_run(const ServerConfig *config, Store *store);

/**
 * Legacy blocking helper for one already-connected descriptor. It remains for
 * focused tests and compatibility; the event loop does not call it.
 * This function takes ownership of client_fd and closes it exactly once.
 */
ServerResult server_handle_client(int client_fd, Store *store);

/** Legacy blocking send helper; the event loop uses buffered partial sends. */
ServerIoResult server_send_all(int socket_fd,
                               const unsigned char *buffer,
                               size_t length);

/** Sets O_NONBLOCK while preserving all existing descriptor status flags. */
ServerResult server_set_nonblocking(int socket_fd);

/** Requests a running server loop to stop. The poll timeout bounds wake-up. */
void server_request_stop(void);

const char *server_result_string(ServerResult result);
const char *server_io_result_string(ServerIoResult result);

#ifdef __cplusplus
}
#endif

#endif
