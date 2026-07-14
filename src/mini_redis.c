#include "mini_redis.h"

#include "command.h"

#include <stdbool.h>

static MiniRedisProcessResult serialize_reply(const RespReply *reply,
                                               unsigned char *output,
                                               size_t output_capacity,
                                               size_t *output_length) {
    const RespSerializeResult result = resp_serialize_reply(
        reply,
        (char *) output,
        output_capacity,
        output_length);

    switch (result) {
        case RESP_SERIALIZE_OK:
            return MINI_REDIS_PROCESS_OK;
        case RESP_SERIALIZE_BUFFER_TOO_SMALL:
            return MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL;
        case RESP_SERIALIZE_INVALID_ARGUMENT:
        default:
            return MINI_REDIS_PROCESS_INTERNAL_ERROR;
    }
}

static MiniRedisProcessResult serialize_parse_error(
    RespParseResult parse_result,
    unsigned char *output,
    size_t output_capacity,
    size_t *output_length) {
    const char *message = NULL;
    MiniRedisProcessResult process_result = MINI_REDIS_PROCESS_INTERNAL_ERROR;

    switch (parse_result) {
        case RESP_PARSE_MALFORMED:
            message = "ERR protocol error";
            process_result = MINI_REDIS_PROCESS_PROTOCOL_ERROR;
            break;
        case RESP_PARSE_TOO_LARGE:
            message = "ERR protocol limit exceeded";
            process_result = MINI_REDIS_PROCESS_LIMIT_ERROR;
            break;
        case RESP_PARSE_OUT_OF_MEMORY:
            message = "ERR out of memory";
            process_result = MINI_REDIS_PROCESS_OUT_OF_MEMORY;
            break;
        default:
            return MINI_REDIS_PROCESS_INTERNAL_ERROR;
    }

    const RespReply reply = resp_reply_error(message);
    const MiniRedisProcessResult serialization = serialize_reply(
        &reply,
        output,
        output_capacity,
        output_length);

    if (serialization != MINI_REDIS_PROCESS_OK) {
        return serialization;
    }
    return process_result;
}

MiniRedisProcessResult mini_redis_process_one(
    Store *store,
    const unsigned char *input,
    size_t input_length,
    size_t *bytes_consumed,
    unsigned char *output,
    size_t output_capacity,
    size_t *output_length) {
    if (bytes_consumed == NULL || output_length == NULL) {
        return MINI_REDIS_PROCESS_INVALID_ARGUMENT;
    }

    *bytes_consumed = 0U;
    *output_length = 0U;

    if (store == NULL || input == NULL || output == NULL || output_capacity == 0U) {
        return MINI_REDIS_PROCESS_INVALID_ARGUMENT;
    }

    Command *command = NULL;
    const RespParseResult parse_result = resp_parse_command(
        (const char *) input,
        input_length,
        &command,
        bytes_consumed);

    if (parse_result == RESP_PARSE_INCOMPLETE) {
        if (input_length >= RESP_MAX_COMMAND_SIZE) {
            return serialize_parse_error(
                RESP_PARSE_TOO_LARGE,
                output,
                output_capacity,
                output_length);
        }
        return MINI_REDIS_PROCESS_INCOMPLETE;
    }

    if (parse_result != RESP_PARSE_OK) {
        if (parse_result == RESP_PARSE_INVALID_ARGUMENT) {
            return MINI_REDIS_PROCESS_INVALID_ARGUMENT;
        }
        return serialize_parse_error(
            parse_result,
            output,
            output_capacity,
            output_length);
    }

    if (command == NULL || *bytes_consumed == 0U || *bytes_consumed > input_length) {
        command_destroy(command);
        *bytes_consumed = 0U;
        return MINI_REDIS_PROCESS_INTERNAL_ERROR;
    }

    const RespReply reply = command_execute(store, command);

    /*
     * Serialize before command_destroy(): PING message and ECHO replies borrow
     * command argument memory. GET replies similarly borrow Store memory.
     */
    const MiniRedisProcessResult serialization = serialize_reply(
        &reply,
        output,
        output_capacity,
        output_length);

    command_destroy(command);

    if (serialization != MINI_REDIS_PROCESS_OK) {
        *bytes_consumed = 0U;
        *output_length = 0U;
        return serialization;
    }

    return MINI_REDIS_PROCESS_OK;
}

static bool config_is_valid(const MiniRedisConfig *config) {
    return config != NULL &&
           config->bind_address != NULL &&
           config->bind_address[0] != '\0' &&
           config->listen_backlog > 0;
}

MiniRedisResult mini_redis_run(const MiniRedisConfig *config) {
    if (!config_is_valid(config)) {
        return MINI_REDIS_INVALID_CONFIGURATION;
    }

    Store *store = store_create();
    if (store == NULL) {
        return MINI_REDIS_STORE_CREATION_FAILED;
    }

    const ServerResult server_result = server_run(config, store);
    store_destroy(store);

    switch (server_result) {
        case SERVER_OK:
        case SERVER_STOPPED:
            return MINI_REDIS_OK;
        case SERVER_INVALID_ARGUMENT:
        case SERVER_ADDRESS_ERROR:
            return MINI_REDIS_INVALID_CONFIGURATION;
        case SERVER_SOCKET_ERROR:
        case SERVER_SOCKET_OPTION_ERROR:
        case SERVER_BIND_ERROR:
        case SERVER_LISTEN_ERROR:
        case SERVER_SIGNAL_ERROR:
            return MINI_REDIS_SERVER_START_FAILED;
        case SERVER_ACCEPT_ERROR:
        case SERVER_CLIENT_IO_ERROR:
        case SERVER_CLIENT_PROTOCOL_ERROR:
        case SERVER_INTERNAL_ERROR:
        default:
            return MINI_REDIS_RUNTIME_ERROR;
    }
}

const char *mini_redis_process_result_string(MiniRedisProcessResult result) {
    switch (result) {
        case MINI_REDIS_PROCESS_OK: return "MINI_REDIS_PROCESS_OK";
        case MINI_REDIS_PROCESS_INCOMPLETE: return "MINI_REDIS_PROCESS_INCOMPLETE";
        case MINI_REDIS_PROCESS_PROTOCOL_ERROR: return "MINI_REDIS_PROCESS_PROTOCOL_ERROR";
        case MINI_REDIS_PROCESS_LIMIT_ERROR: return "MINI_REDIS_PROCESS_LIMIT_ERROR";
        case MINI_REDIS_PROCESS_OUT_OF_MEMORY: return "MINI_REDIS_PROCESS_OUT_OF_MEMORY";
        case MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL: return "MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL";
        case MINI_REDIS_PROCESS_INVALID_ARGUMENT: return "MINI_REDIS_PROCESS_INVALID_ARGUMENT";
        case MINI_REDIS_PROCESS_INTERNAL_ERROR: return "MINI_REDIS_PROCESS_INTERNAL_ERROR";
        default: return "MINI_REDIS_PROCESS_UNKNOWN_RESULT";
    }
}

const char *mini_redis_result_string(MiniRedisResult result) {
    switch (result) {
        case MINI_REDIS_OK: return "MINI_REDIS_OK";
        case MINI_REDIS_INVALID_CONFIGURATION: return "MINI_REDIS_INVALID_CONFIGURATION";
        case MINI_REDIS_STORE_CREATION_FAILED: return "MINI_REDIS_STORE_CREATION_FAILED";
        case MINI_REDIS_SERVER_START_FAILED: return "MINI_REDIS_SERVER_START_FAILED";
        case MINI_REDIS_RUNTIME_ERROR: return "MINI_REDIS_RUNTIME_ERROR";
        default: return "MINI_REDIS_UNKNOWN_RESULT";
    }
}
