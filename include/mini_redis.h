#ifndef MINI_REDIS_H
#define MINI_REDIS_H

#include "resp.h"
#include "server.h"
#include "store.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINI_REDIS_MAX_REPLY_SIZE (RESP_MAX_COMMAND_SIZE + 64U)

typedef ServerConfig MiniRedisConfig;

typedef enum MiniRedisProcessResult {
    MINI_REDIS_PROCESS_OK = 0,
    MINI_REDIS_PROCESS_INCOMPLETE,
    MINI_REDIS_PROCESS_PROTOCOL_ERROR,
    MINI_REDIS_PROCESS_LIMIT_ERROR,
    MINI_REDIS_PROCESS_OUT_OF_MEMORY,
    MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL,
    MINI_REDIS_PROCESS_INVALID_ARGUMENT,
    MINI_REDIS_PROCESS_INTERNAL_ERROR
} MiniRedisProcessResult;

typedef enum MiniRedisResult {
    MINI_REDIS_OK = 0,
    MINI_REDIS_INVALID_CONFIGURATION,
    MINI_REDIS_STORE_CREATION_FAILED,
    MINI_REDIS_SERVER_START_FAILED,
    MINI_REDIS_RUNTIME_ERROR
} MiniRedisResult;

/**
 * Parses, executes, and serializes at most one RESP command from input.
 *
 * On MINI_REDIS_PROCESS_OK, bytes_consumed and output_length are positive.
 * On parser protocol/limit/allocation errors, an RESP error is serialized when
 * the output buffer is large enough; the caller should send it and close the
 * connection because stream resynchronization is not attempted.
 */
MiniRedisProcessResult mini_redis_process_one(
    Store *store,
    const unsigned char *input,
    size_t input_length,
    size_t *bytes_consumed,
    unsigned char *output,
    size_t output_capacity,
    size_t *output_length);

/** Creates the Store, runs the server, and destroys the Store on return. */
MiniRedisResult mini_redis_run(const MiniRedisConfig *config);

const char *mini_redis_process_result_string(MiniRedisProcessResult result);
const char *mini_redis_result_string(MiniRedisResult result);

#ifdef __cplusplus
}
#endif

#endif
