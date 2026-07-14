#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include "mini_redis.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void stop_signal_handler(int signal_number) {
    (void) signal_number;
    stop_requested = 1;
}

void server_request_stop(void) {
    stop_requested = 1;
}

static void close_fd(int *fd) {
    if (fd != NULL && *fd >= 0) {
        (void) close(*fd);
        *fd = -1;
    }
}

static bool configure_no_sigpipe(int socket_fd) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(socket_fd,
                   SOL_SOCKET,
                   SO_NOSIGPIPE,
                   &enabled,
                   (socklen_t) sizeof(enabled)) < 0) {
        return false;
    }
#else
    (void) socket_fd;
#endif
    return true;
}

ServerIoResult server_send_all(int socket_fd,
                               const unsigned char *buffer,
                               size_t length) {
    if (socket_fd < 0 || (buffer == NULL && length > 0U)) {
        return SERVER_IO_INVALID_ARGUMENT;
    }

    size_t sent = 0U;
    while (sent < length) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const ssize_t result = send(socket_fd,
                                    buffer + sent,
                                    length - sent,
                                    flags);
        if (result > 0) {
            sent += (size_t) result;
            continue;
        }
        if (result == 0) {
            return SERVER_IO_DISCONNECTED;
        }
        if (errno == EINTR) {
            if (stop_requested != 0) {
                return SERVER_IO_DISCONNECTED;
            }
            continue;
        }
        if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
            return SERVER_IO_DISCONNECTED;
        }
        return SERVER_IO_ERROR;
    }

    return SERVER_IO_OK;
}

ServerResult server_handle_client(int client_fd, Store *store) {
    if (client_fd < 0 || store == NULL) {
        if (client_fd >= 0) {
            close_fd(&client_fd);
        }
        return SERVER_INVALID_ARGUMENT;
    }

    unsigned char input_buffer[RESP_MAX_COMMAND_SIZE];
    unsigned char output_buffer[MINI_REDIS_MAX_REPLY_SIZE];
    size_t input_length = 0U;
    ServerResult final_result = SERVER_OK;

    for (;;) {
        while (input_length > 0U) {
            size_t bytes_consumed = 0U;
            size_t output_length = 0U;
            const MiniRedisProcessResult process_result = mini_redis_process_one(
                store,
                input_buffer,
                input_length,
                &bytes_consumed,
                output_buffer,
                sizeof(output_buffer),
                &output_length);

            if (process_result == MINI_REDIS_PROCESS_INCOMPLETE) {
                break;
            }

            if (output_length > 0U) {
                const ServerIoResult send_result = server_send_all(
                    client_fd,
                    output_buffer,
                    output_length);
                if (send_result != SERVER_IO_OK) {
                    final_result = SERVER_CLIENT_IO_ERROR;
                    goto done;
                }
            }

            if (process_result != MINI_REDIS_PROCESS_OK) {
                switch (process_result) {
                    case MINI_REDIS_PROCESS_PROTOCOL_ERROR:
                    case MINI_REDIS_PROCESS_LIMIT_ERROR:
                    case MINI_REDIS_PROCESS_OUT_OF_MEMORY:
                        final_result = SERVER_CLIENT_PROTOCOL_ERROR;
                        break;
                    case MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL:
                    case MINI_REDIS_PROCESS_INVALID_ARGUMENT:
                    case MINI_REDIS_PROCESS_INTERNAL_ERROR:
                    default:
                        final_result = SERVER_INTERNAL_ERROR;
                        break;
                }
                goto done;
            }

            if (bytes_consumed == 0U || bytes_consumed > input_length) {
                final_result = SERVER_INTERNAL_ERROR;
                goto done;
            }

            const size_t remaining = input_length - bytes_consumed;
            if (remaining > 0U) {
                memmove(input_buffer,
                        input_buffer + bytes_consumed,
                        remaining);
            }
            input_length = remaining;
        }

        if (stop_requested != 0) {
            final_result = SERVER_STOPPED;
            goto done;
        }

        if (input_length == sizeof(input_buffer)) {
            /* process_one converts a full incomplete frame into a limit error. */
            size_t ignored_consumed = 0U;
            size_t error_length = 0U;
            const MiniRedisProcessResult process_result = mini_redis_process_one(
                store,
                input_buffer,
                input_length,
                &ignored_consumed,
                output_buffer,
                sizeof(output_buffer),
                &error_length);

            if (error_length > 0U) {
                (void) server_send_all(client_fd, output_buffer, error_length);
            }
            final_result = process_result == MINI_REDIS_PROCESS_LIMIT_ERROR
                ? SERVER_CLIENT_PROTOCOL_ERROR
                : SERVER_INTERNAL_ERROR;
            goto done;
        }

        const ssize_t received = recv(client_fd,
                                      input_buffer + input_length,
                                      sizeof(input_buffer) - input_length,
                                      0);
        if (received > 0) {
            input_length += (size_t) received;
            continue;
        }
        if (received == 0) {
            final_result = SERVER_OK;
            goto done;
        }
        if (errno == EINTR) {
            if (stop_requested != 0) {
                final_result = SERVER_STOPPED;
                goto done;
            }
            continue;
        }
        if (errno == ECONNRESET || errno == ENOTCONN) {
            final_result = SERVER_OK;
            goto done;
        }

        final_result = SERVER_CLIENT_IO_ERROR;
        goto done;
    }

done:
    close_fd(&client_fd);
    return final_result;
}

static ServerResult create_listener(const ServerConfig *config,
                                    int *listener_out,
                                    uint16_t *bound_port_out) {
    int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        fprintf(stderr, "[ERROR] socket creation failed: %s\n", strerror(errno));
        return SERVER_SOCKET_ERROR;
    }

    const int reuse_address = 1;
    if (setsockopt(listener_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse_address,
                   (socklen_t) sizeof(reuse_address)) < 0) {
        fprintf(stderr, "[ERROR] SO_REUSEADDR failed: %s\n", strerror(errno));
        close_fd(&listener_fd);
        return SERVER_SOCKET_OPTION_ERROR;
    }

    if (!configure_no_sigpipe(listener_fd)) {
        fprintf(stderr, "[ERROR] SIGPIPE socket configuration failed: %s\n", strerror(errno));
        close_fd(&listener_fd);
        return SERVER_SOCKET_OPTION_ERROR;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(config->port);

    const int address_result = inet_pton(
        AF_INET,
        config->bind_address,
        &address.sin_addr);
    if (address_result != 1) {
        fprintf(stderr, "[ERROR] invalid IPv4 bind address: %s\n", config->bind_address);
        close_fd(&listener_fd);
        return SERVER_ADDRESS_ERROR;
    }

    if (bind(listener_fd,
             (const struct sockaddr *) &address,
             (socklen_t) sizeof(address)) < 0) {
        fprintf(stderr, "[ERROR] bind failed on %s:%u: %s\n",
                config->bind_address,
                (unsigned) config->port,
                strerror(errno));
        close_fd(&listener_fd);
        return SERVER_BIND_ERROR;
    }

    if (listen(listener_fd, config->listen_backlog) < 0) {
        fprintf(stderr, "[ERROR] listen failed: %s\n", strerror(errno));
        close_fd(&listener_fd);
        return SERVER_LISTEN_ERROR;
    }

    struct sockaddr_in bound_address;
    socklen_t bound_length = (socklen_t) sizeof(bound_address);
    if (getsockname(listener_fd,
                    (struct sockaddr *) &bound_address,
                    &bound_length) == 0) {
        *bound_port_out = ntohs(bound_address.sin_port);
    } else {
        *bound_port_out = config->port;
    }

    *listener_out = listener_fd;
    return SERVER_OK;
}

static bool install_stop_handlers(struct sigaction *old_sigint,
                                  struct sigaction *old_sigterm) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, old_sigint) < 0) {
        return false;
    }
    if (sigaction(SIGTERM, &action, old_sigterm) < 0) {
        (void) sigaction(SIGINT, old_sigint, NULL);
        return false;
    }
    return true;
}

static void restore_stop_handlers(const struct sigaction *old_sigint,
                                  const struct sigaction *old_sigterm) {
    (void) sigaction(SIGINT, old_sigint, NULL);
    (void) sigaction(SIGTERM, old_sigterm, NULL);
}

ServerResult server_run(const ServerConfig *config, Store *store) {
    if (config == NULL || store == NULL ||
        config->bind_address == NULL ||
        config->bind_address[0] == '\0' ||
        config->listen_backlog <= 0) {
        return SERVER_INVALID_ARGUMENT;
    }

    stop_requested = 0;

    struct sigaction old_sigint;
    struct sigaction old_sigterm;
    if (!install_stop_handlers(&old_sigint, &old_sigterm)) {
        fprintf(stderr, "[ERROR] signal handler installation failed: %s\n", strerror(errno));
        return SERVER_SIGNAL_ERROR;
    }

    int listener_fd = -1;
    uint16_t bound_port = config->port;
    ServerResult result = create_listener(config, &listener_fd, &bound_port);
    if (result != SERVER_OK) {
        restore_stop_handlers(&old_sigint, &old_sigterm);
        return result;
    }

    fprintf(stderr,
            "[INFO] Mini Redis listening on %s:%u\n",
            config->bind_address,
            (unsigned) bound_port);

    for (;;) {
        if (stop_requested != 0) {
            result = SERVER_STOPPED;
            break;
        }

        const int client_fd = accept(listener_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (stop_requested != 0) {
                    result = SERVER_STOPPED;
                    break;
                }
                continue;
            }
            fprintf(stderr, "[ERROR] accept failed: %s\n", strerror(errno));
            result = SERVER_ACCEPT_ERROR;
            break;
        }

        if (!configure_no_sigpipe(client_fd)) {
            int fd_to_close = client_fd;
            close_fd(&fd_to_close);
            result = SERVER_SOCKET_OPTION_ERROR;
            break;
        }

        fprintf(stderr, "[INFO] client connected\n");
        const ServerResult client_result = server_handle_client(client_fd, store);
        fprintf(stderr, "[INFO] client disconnected\n");

        if (client_result == SERVER_STOPPED) {
            result = SERVER_STOPPED;
            break;
        }
        if (client_result == SERVER_INTERNAL_ERROR) {
            result = client_result;
            break;
        }
        if (client_result == SERVER_CLIENT_PROTOCOL_ERROR) {
            fprintf(stderr, "[WARN] client protocol error\n");
        } else if (client_result == SERVER_CLIENT_IO_ERROR) {
            fprintf(stderr, "[WARN] client I/O error\n");
        } else if (client_result != SERVER_OK) {
            result = client_result;
            break;
        }

        if (!config->continue_after_client_disconnect) {
            result = client_result == SERVER_OK ? SERVER_OK : client_result;
            break;
        }
    }

    close_fd(&listener_fd);
    restore_stop_handlers(&old_sigint, &old_sigterm);
    return result;
}

const char *server_result_string(ServerResult result) {
    switch (result) {
        case SERVER_OK: return "SERVER_OK";
        case SERVER_STOPPED: return "SERVER_STOPPED";
        case SERVER_INVALID_ARGUMENT: return "SERVER_INVALID_ARGUMENT";
        case SERVER_SOCKET_ERROR: return "SERVER_SOCKET_ERROR";
        case SERVER_SOCKET_OPTION_ERROR: return "SERVER_SOCKET_OPTION_ERROR";
        case SERVER_ADDRESS_ERROR: return "SERVER_ADDRESS_ERROR";
        case SERVER_BIND_ERROR: return "SERVER_BIND_ERROR";
        case SERVER_LISTEN_ERROR: return "SERVER_LISTEN_ERROR";
        case SERVER_SIGNAL_ERROR: return "SERVER_SIGNAL_ERROR";
        case SERVER_ACCEPT_ERROR: return "SERVER_ACCEPT_ERROR";
        case SERVER_CLIENT_IO_ERROR: return "SERVER_CLIENT_IO_ERROR";
        case SERVER_CLIENT_PROTOCOL_ERROR: return "SERVER_CLIENT_PROTOCOL_ERROR";
        case SERVER_INTERNAL_ERROR: return "SERVER_INTERNAL_ERROR";
        default: return "SERVER_UNKNOWN_RESULT";
    }
}

const char *server_io_result_string(ServerIoResult result) {
    switch (result) {
        case SERVER_IO_OK: return "SERVER_IO_OK";
        case SERVER_IO_INVALID_ARGUMENT: return "SERVER_IO_INVALID_ARGUMENT";
        case SERVER_IO_DISCONNECTED: return "SERVER_IO_DISCONNECTED";
        case SERVER_IO_ERROR: return "SERVER_IO_ERROR";
        default: return "SERVER_IO_UNKNOWN_RESULT";
    }
}
