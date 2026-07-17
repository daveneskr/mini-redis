#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include "mini_redis.h"
#include "server_internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

typedef enum ClientEventResult {
    CLIENT_EVENT_OK = 0,
    CLIENT_EVENT_REMOVE,
    CLIENT_EVENT_STOPPED,
    CLIENT_EVENT_FATAL
} ClientEventResult;

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

static int send_flags(void) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    return flags;
}

ServerResult server_set_nonblocking(int socket_fd) {
    if (socket_fd < 0) {
        return SERVER_INVALID_ARGUMENT;
    }

    const int current_flags = fcntl(socket_fd, F_GETFL, 0);
    if (current_flags < 0) {
        return SERVER_NONBLOCKING_ERROR;
    }

    if ((current_flags & O_NONBLOCK) != 0) {
        return SERVER_OK;
    }

    if (fcntl(socket_fd, F_SETFL, current_flags | O_NONBLOCK) < 0) {
        return SERVER_NONBLOCKING_ERROR;
    }

    return SERVER_OK;
}

ServerIoResult server_send_all(int socket_fd,
                               const unsigned char *buffer,
                               size_t length) {
    if (socket_fd < 0 || (buffer == NULL && length > 0U)) {
        return SERVER_IO_INVALID_ARGUMENT;
    }

    size_t sent = 0U;
    while (sent < length) {
        const ssize_t result = send(socket_fd,
                                    buffer + sent,
                                    length - sent,
                                    send_flags());
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
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return SERVER_IO_WOULD_BLOCK;
        }
        if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
            return SERVER_IO_DISCONNECTED;
        }
        return SERVER_IO_ERROR;
    }

    return SERVER_IO_OK;
}

/*
 * Legacy blocking session processor. The event loop below intentionally does
 * not call this function because it owns per-client buffering and partial I/O.
 */
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
    if (config == NULL || listener_out == NULL || bound_port_out == NULL) {
        return SERVER_INVALID_ARGUMENT;
    }

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
        fprintf(stderr,
                "[ERROR] SIGPIPE socket configuration failed: %s\n",
                strerror(errno));
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
        fprintf(stderr,
                "[ERROR] invalid IPv4 bind address: %s\n",
                config->bind_address);
        close_fd(&listener_fd);
        return SERVER_ADDRESS_ERROR;
    }

    if (bind(listener_fd,
             (const struct sockaddr *) &address,
             (socklen_t) sizeof(address)) < 0) {
        fprintf(stderr,
                "[ERROR] bind failed on %s:%u: %s\n",
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

    const ServerResult nonblocking_result = server_set_nonblocking(listener_fd);
    if (nonblocking_result != SERVER_OK) {
        fprintf(stderr,
                "[ERROR] listener non-blocking configuration failed: %s\n",
                strerror(errno));
        close_fd(&listener_fd);
        return nonblocking_result;
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

ServerResult server_initialize(Server *server,
                               const ServerConfig *config,
                               Store *store,
                               uint16_t *bound_port_out) {
    if (server == NULL || config == NULL || store == NULL ||
        bound_port_out == NULL || config->bind_address == NULL ||
        config->bind_address[0] == '\0' || config->listen_backlog <= 0) {
        return SERVER_INVALID_ARGUMENT;
    }

    memset(server, 0, sizeof(*server));
    server->listener_fd = -1;
    server->store = store;
    server->config = *config;
    server->config.max_clients = server_effective_max_clients(config);
    server->accepting_clients = true;

    if (server->config.max_clients == 0U ||
        server->config.max_clients > SIZE_MAX - 1U ||
        server->config.max_clients + 1U > SIZE_MAX / sizeof(*server->poll_fds)) {
        return SERVER_INVALID_ARGUMENT;
    }

    server->clients = client_registry_create(server->config.max_clients);
    if (server->clients == NULL) {
        return SERVER_INTERNAL_ERROR;
    }

    server->poll_capacity = server->config.max_clients + 1U;
    server->poll_fds = calloc(server->poll_capacity, sizeof(*server->poll_fds));
    if (server->poll_fds == NULL) {
        server_destroy(server);
        return SERVER_INTERNAL_ERROR;
    }

    const ServerResult listener_result = create_listener(
        &server->config,
        &server->listener_fd,
        bound_port_out);
    if (listener_result != SERVER_OK) {
        server_destroy(server);
        return listener_result;
    }

    return SERVER_OK;
}

void server_destroy(Server *server) {
    if (server == NULL) {
        return;
    }

    close_fd(&server->listener_fd);
    client_registry_destroy(server->clients);
    server->clients = NULL;
    free(server->poll_fds);
    server->poll_fds = NULL;
    server->poll_capacity = 0U;
    server->stats.active_clients = 0U;
    server->accepting_clients = false;
}

static void update_peak_clients(Server *server) {
    if (server->stats.active_clients > server->stats.peak_clients) {
        server->stats.peak_clients = server->stats.active_clients;
    }
}

static void remove_client(Server *server, int socket_fd, const char *reason) {
    const ClientRegistryResult remove_result = client_registry_remove_by_fd(
        server->clients,
        socket_fd);
    if (remove_result != CLIENT_REGISTRY_OK) {
        return;
    }

    if (server->stats.active_clients > 0U) {
        --server->stats.active_clients;
    }
    fprintf(stderr,
            "[INFO] client disconnected fd=%d active_clients=%llu reason=%s\n",
            socket_fd,
            server->stats.active_clients,
            reason == NULL ? "closed" : reason);
}

static ClientEventResult process_buffered_input(Server *server,
                                                ClientSession *client) {
    if (server == NULL || client == NULL) {
        return CLIENT_EVENT_FATAL;
    }

    if (client_session_has_pending_output(client) || client->close_requested) {
        return CLIENT_EVENT_OK;
    }

    if (client->input_length == 0U) {
        if (client->peer_eof) {
            client->close_requested = true;
        }
        return CLIENT_EVENT_OK;
    }

    size_t bytes_consumed = 0U;
    size_t output_length = 0U;
    const MiniRedisProcessResult process_result = mini_redis_process_one(
        server->store,
        client->input_buffer,
        client->input_length,
        &bytes_consumed,
        client->output_buffer,
        sizeof(client->output_buffer),
        &output_length);

    client->output_offset = 0U;
    client->output_length = output_length;

    switch (process_result) {
        case MINI_REDIS_PROCESS_OK:
            if (bytes_consumed == 0U ||
                !client_session_consume_input(client, bytes_consumed) ||
                output_length == 0U) {
                return CLIENT_EVENT_FATAL;
            }
            ++client->commands_processed;
            ++server->stats.completed_commands;
            return CLIENT_EVENT_OK;

        case MINI_REDIS_PROCESS_INCOMPLETE:
            client_session_reset_output(client);
            if (client->peer_eof) {
                client->input_length = 0U;
                client->close_requested = true;
            }
            return CLIENT_EVENT_OK;

        case MINI_REDIS_PROCESS_PROTOCOL_ERROR:
        case MINI_REDIS_PROCESS_LIMIT_ERROR:
            ++server->stats.protocol_errors;
            client->input_length = 0U;
            client->close_requested = true;
            if (output_length == 0U) {
                return CLIENT_EVENT_FATAL;
            }
            return CLIENT_EVENT_OK;

        case MINI_REDIS_PROCESS_OUT_OF_MEMORY:
            client->input_length = 0U;
            client->close_requested = true;
            if (output_length == 0U) {
                return CLIENT_EVENT_FATAL;
            }
            return CLIENT_EVENT_OK;

        case MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL:
        case MINI_REDIS_PROCESS_INVALID_ARGUMENT:
        case MINI_REDIS_PROCESS_INTERNAL_ERROR:
        default:
            client_session_reset_output(client);
            return CLIENT_EVENT_FATAL;
    }
}

static ClientEventResult read_from_client(Server *server,
                                          ClientSession *client) {
    if (server == NULL || client == NULL) {
        return CLIENT_EVENT_FATAL;
    }

    if (client_session_has_pending_output(client) || client->close_requested) {
        return CLIENT_EVENT_OK;
    }

    for (size_t reads = 0U;
         reads < MINI_REDIS_MAX_READS_PER_TICK;
         ++reads) {
        if (client_session_input_is_full(client)) {
            break;
        }

        const size_t remaining = sizeof(client->input_buffer) - client->input_length;
        const ssize_t received = recv(client->socket_fd,
                                      client->input_buffer + client->input_length,
                                      remaining,
                                      0);
        if (received > 0) {
            client->input_length += (size_t) received;
            continue;
        }
        if (received == 0) {
            client->peer_eof = true;
            break;
        }
        if (errno == EINTR) {
            if (stop_requested != 0) {
                return CLIENT_EVENT_STOPPED;
            }
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == ECONNRESET || errno == ENOTCONN) {
            return CLIENT_EVENT_REMOVE;
        }

        ++server->stats.receive_errors;
        fprintf(stderr,
                "[WARN] recv failed fd=%d error=%s\n",
                client->socket_fd,
                strerror(errno));
        return CLIENT_EVENT_REMOVE;
    }

    return process_buffered_input(server, client);
}

static ClientEventResult write_to_client(Server *server,
                                         ClientSession *client) {
    if (server == NULL || client == NULL) {
        return CLIENT_EVENT_FATAL;
    }

    for (size_t writes = 0U;
         writes < MINI_REDIS_MAX_WRITES_PER_TICK &&
         client_session_has_pending_output(client);
         ++writes) {
        const size_t remaining = client->output_length - client->output_offset;
        const ssize_t sent = send(client->socket_fd,
                                  client->output_buffer + client->output_offset,
                                  remaining,
                                  send_flags());
        if (sent > 0) {
            client->output_offset += (size_t) sent;
            continue;
        }
        if (sent == 0) {
            return CLIENT_EVENT_REMOVE;
        }
        if (errno == EINTR) {
            if (stop_requested != 0) {
                return CLIENT_EVENT_STOPPED;
            }
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return CLIENT_EVENT_OK;
        }
        if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
            return CLIENT_EVENT_REMOVE;
        }

        ++server->stats.send_errors;
        fprintf(stderr,
                "[WARN] send failed fd=%d error=%s\n",
                client->socket_fd,
                strerror(errno));
        return CLIENT_EVENT_REMOVE;
    }

    if (client->output_offset == client->output_length) {
        client_session_reset_output(client);
        return process_buffered_input(server, client);
    }

    return CLIENT_EVENT_OK;
}

static ServerResult accept_ready_clients(Server *server) {
    for (size_t accepted_this_tick = 0U;
         accepted_this_tick < MINI_REDIS_MAX_ACCEPTS_PER_TICK;
         ++accepted_this_tick) {
        const int client_fd = accept(server->listener_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (stop_requested != 0) {
                    return SERVER_STOPPED;
                }
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return SERVER_OK;
            }
            fprintf(stderr, "[ERROR] accept failed: %s\n", strerror(errno));
            return SERVER_ACCEPT_ERROR;
        }

        ++server->stats.accepted_connections;

        int fd_to_close = client_fd;
        if (!configure_no_sigpipe(client_fd)) {
            fprintf(stderr,
                    "[WARN] client SIGPIPE configuration failed fd=%d error=%s\n",
                    client_fd,
                    strerror(errno));
            close_fd(&fd_to_close);
            ++server->stats.rejected_connections;
            continue;
        }

        const ServerResult nonblocking_result = server_set_nonblocking(client_fd);
        if (nonblocking_result != SERVER_OK) {
            fprintf(stderr,
                    "[WARN] client non-blocking configuration failed fd=%d error=%s\n",
                    client_fd,
                    strerror(errno));
            close_fd(&fd_to_close);
            ++server->stats.rejected_connections;
            continue;
        }

        if (client_registry_count(server->clients) >=
            client_registry_capacity(server->clients)) {
            ++server->stats.rejected_connections;
            fprintf(stderr,
                    "[WARN] client rejected fd=%d reason=max_clients\n",
                    client_fd);
            close_fd(&fd_to_close);
            continue;
        }

        ClientSession *client = client_session_create(client_fd);
        if (client == NULL) {
            close_fd(&fd_to_close);
            return SERVER_INTERNAL_ERROR;
        }

        const ClientRegistryResult add_result = client_registry_add(
            server->clients,
            client);
        if (add_result != CLIENT_REGISTRY_OK) {
            client_session_destroy(client);
            if (add_result == CLIENT_REGISTRY_LIMIT_REACHED) {
                ++server->stats.rejected_connections;
                continue;
            }
            return SERVER_INTERNAL_ERROR;
        }

        fd_to_close = -1;
        ++server->stats.active_clients;
        update_peak_clients(server);
        fprintf(stderr,
                "[INFO] client connected fd=%d active_clients=%llu\n",
                client_fd,
                server->stats.active_clients);

        server->accepted_once = true;
        if (!server->config.continue_after_client_disconnect) {
            server->accepting_clients = false;
            return SERVER_OK;
        }
    }

    return SERVER_OK;
}

static size_t rebuild_poll_set(Server *server) {
    size_t poll_count = 0U;

    if (server->accepting_clients) {
        server->poll_fds[poll_count].fd = server->listener_fd;
        server->poll_fds[poll_count].events = POLLIN;
        server->poll_fds[poll_count].revents = 0;
        ++poll_count;
    }

    const size_t client_count = client_registry_count(server->clients);
    for (size_t i = 0U; i < client_count; ++i) {
        ClientSession *client = client_registry_get(server->clients, i);
        if (client == NULL || poll_count >= server->poll_capacity) {
            break;
        }

        short events = 0;
        if (client_session_has_pending_output(client)) {
            events |= POLLOUT;
        } else if (!client->close_requested && !client->peer_eof) {
            events |= POLLIN;
        }

        server->poll_fds[poll_count].fd = client->socket_fd;
        server->poll_fds[poll_count].events = events;
        server->poll_fds[poll_count].revents = 0;
        ++poll_count;
    }

    return poll_count;
}

static ServerResult handle_client_event(Server *server,
                                        int socket_fd,
                                        short revents) {
    ClientSession *client = client_registry_find_by_fd(server->clients, socket_fd);
    if (client == NULL) {
        return SERVER_OK;
    }

    ClientEventResult event_result = CLIENT_EVENT_OK;

    if ((revents & (POLLIN | POLLHUP)) != 0 &&
        !client_session_has_pending_output(client)) {
        event_result = read_from_client(server, client);
    }

    if (event_result == CLIENT_EVENT_OK &&
        (revents & (POLLOUT | POLLHUP)) != 0 &&
        client_session_has_pending_output(client)) {
        event_result = write_to_client(server, client);
    }

    if (event_result == CLIENT_EVENT_STOPPED) {
        return SERVER_STOPPED;
    }
    if (event_result == CLIENT_EVENT_FATAL) {
        return SERVER_INTERNAL_ERROR;
    }
    if (event_result == CLIENT_EVENT_REMOVE) {
        remove_client(server, socket_fd, "socket_error");
        return SERVER_OK;
    }

    client = client_registry_find_by_fd(server->clients, socket_fd);
    if (client == NULL) {
        return SERVER_OK;
    }

    if ((revents & (POLLERR | POLLNVAL)) != 0) {
        ++server->stats.receive_errors;
        remove_client(server, socket_fd, "poll_error");
        return SERVER_OK;
    }

    if (client_session_ready_to_close(client)) {
        remove_client(server, socket_fd, "drained");
    }

    return SERVER_OK;
}

ServerResult server_event_loop(Server *server) {
    if (server == NULL || server->listener_fd < 0 || server->store == NULL ||
        server->clients == NULL || server->poll_fds == NULL) {
        return SERVER_INVALID_ARGUMENT;
    }

    for (;;) {
        if (stop_requested != 0) {
            return SERVER_STOPPED;
        }

        if (!server->accepting_clients &&
            server->accepted_once &&
            client_registry_count(server->clients) == 0U) {
            return SERVER_OK;
        }

        const size_t poll_count = rebuild_poll_set(server);
        if (poll_count == 0U) {
            return SERVER_OK;
        }

        const int ready = poll(server->poll_fds,
                               (nfds_t) poll_count,
                               MINI_REDIS_POLL_TIMEOUT_MS);
        if (ready < 0) {
            if (errno == EINTR) {
                if (stop_requested != 0) {
                    return SERVER_STOPPED;
                }
                continue;
            }
            fprintf(stderr, "[ERROR] poll failed: %s\n", strerror(errno));
            return SERVER_POLL_ERROR;
        }
        if (ready == 0) {
            continue;
        }

        size_t first_client_index = 0U;
        if (server->accepting_clients && poll_count > 0U &&
            server->poll_fds[0].fd == server->listener_fd) {
            first_client_index = 1U;
            const short listener_events = server->poll_fds[0].revents;
            if ((listener_events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                return SERVER_ACCEPT_ERROR;
            }
            if ((listener_events & POLLIN) != 0) {
                const ServerResult accept_result = accept_ready_clients(server);
                if (accept_result != SERVER_OK) {
                    return accept_result;
                }
            }
        }

        /*
         * Process the readiness snapshot in reverse. Client registry removal
         * uses swap-with-last, but descriptors in this snapshot remain stable.
         */
        for (size_t i = poll_count; i > first_client_index; --i) {
            const struct pollfd ready_fd = server->poll_fds[i - 1U];
            if (ready_fd.revents == 0) {
                continue;
            }

            const ServerResult client_result = handle_client_event(
                server,
                ready_fd.fd,
                ready_fd.revents);
            if (client_result != SERVER_OK) {
                return client_result;
            }
        }
    }
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
        fprintf(stderr,
                "[ERROR] signal handler installation failed: %s\n",
                strerror(errno));
        return SERVER_SIGNAL_ERROR;
    }

    Server server;
    uint16_t bound_port = config->port;
    ServerResult result = server_initialize(
        &server,
        config,
        store,
        &bound_port);
    if (result != SERVER_OK) {
        restore_stop_handlers(&old_sigint, &old_sigterm);
        return result;
    }

    fprintf(stderr,
            "[INFO] Mini Redis listening on %s:%u max_clients=%zu\n",
            config->bind_address,
            (unsigned) bound_port,
            server.config.max_clients);

    result = server_event_loop(&server);

    fprintf(stderr,
            "[INFO] server stopping accepted=%llu peak_clients=%llu commands=%llu "
            "protocol_errors=%llu rejected=%llu recv_errors=%llu send_errors=%llu\n",
            server.stats.accepted_connections,
            server.stats.peak_clients,
            server.stats.completed_commands,
            server.stats.protocol_errors,
            server.stats.rejected_connections,
            server.stats.receive_errors,
            server.stats.send_errors);

    server_destroy(&server);
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
        case SERVER_NONBLOCKING_ERROR: return "SERVER_NONBLOCKING_ERROR";
        case SERVER_ADDRESS_ERROR: return "SERVER_ADDRESS_ERROR";
        case SERVER_BIND_ERROR: return "SERVER_BIND_ERROR";
        case SERVER_LISTEN_ERROR: return "SERVER_LISTEN_ERROR";
        case SERVER_SIGNAL_ERROR: return "SERVER_SIGNAL_ERROR";
        case SERVER_ACCEPT_ERROR: return "SERVER_ACCEPT_ERROR";
        case SERVER_POLL_ERROR: return "SERVER_POLL_ERROR";
        case SERVER_CLIENT_IO_ERROR: return "SERVER_CLIENT_IO_ERROR";
        case SERVER_CLIENT_PROTOCOL_ERROR: return "SERVER_CLIENT_PROTOCOL_ERROR";
        case SERVER_INTERNAL_ERROR: return "SERVER_INTERNAL_ERROR";
        default: return "SERVER_UNKNOWN_RESULT";
    }
}

const char *server_io_result_string(ServerIoResult result) {
    switch (result) {
        case SERVER_IO_OK: return "SERVER_IO_OK";
        case SERVER_IO_WOULD_BLOCK: return "SERVER_IO_WOULD_BLOCK";
        case SERVER_IO_INVALID_ARGUMENT: return "SERVER_IO_INVALID_ARGUMENT";
        case SERVER_IO_DISCONNECTED: return "SERVER_IO_DISCONNECTED";
        case SERVER_IO_ERROR: return "SERVER_IO_ERROR";
        default: return "SERVER_IO_UNKNOWN_RESULT";
    }
}
