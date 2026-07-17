#ifndef MINI_REDIS_CLIENT_SESSION_H
#define MINI_REDIS_CLIENT_SESSION_H

#include "mini_redis_limits.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Per-client state owned by the server event-loop layer.
 *
 * Ownership:
 *   - socket_fd is owned by the ClientSession after successful creation.
 *   - client_session_destroy closes socket_fd exactly once when it is >= 0.
 *   - input_buffer and output_buffer are fixed-size, bounded buffers owned by
 *     the session.
 *
 * Backpressure model:
 *   - at most one serialized reply is stored in output_buffer;
 *   - input parsing/execution pauses while output is pending;
 *   - buffered pipelined input resumes after that reply is fully sent.
 *
 * Closing model:
 *   - peer_eof means no more bytes can be read from the peer;
 *   - close_requested means close after pending output is sent;
 *   - fatal socket errors are handled by immediate registry removal.
 */
typedef struct ClientSession {
    int socket_fd;

    unsigned char input_buffer[RESP_MAX_COMMAND_SIZE];
    size_t input_length;

    unsigned char output_buffer[MINI_REDIS_MAX_REPLY_SIZE];
    size_t output_length;
    size_t output_offset;

    unsigned long long commands_processed;

    bool peer_eof;
    bool close_requested;
} ClientSession;

/**
 * Creates a client session and takes ownership of socket_fd.
 * Returns NULL for an invalid descriptor or allocation failure.
 */
ClientSession *client_session_create(int socket_fd);

/** Frees the session and closes its owned socket descriptor. Safe for NULL. */
void client_session_destroy(ClientSession *client);

/** Returns the owned socket descriptor, or -1 for NULL. */
int client_session_socket_fd(const ClientSession *client);

/** Returns true when there are unsent bytes in the output buffer. */
bool client_session_has_pending_output(const ClientSession *client);

/** Returns true when no more input can be appended. */
bool client_session_input_is_full(const ClientSession *client);

/** Removes consumed bytes from the front of input_buffer. */
bool client_session_consume_input(ClientSession *client, size_t consumed);

/** Clears output state after a complete send. */
void client_session_reset_output(ClientSession *client);

/** Returns true when the session can now be removed cleanly. */
bool client_session_ready_to_close(const ClientSession *client);

#ifdef __cplusplus
}
#endif

#endif
