#include "resp.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_crlf_at(const char *buffer, size_t buffer_len, size_t index) {
    return index + 1U < buffer_len && buffer[index] == '\r' && buffer[index + 1U] == '\n';
}

static RespParseResult parse_unsigned_line(const char *buffer,
                                           size_t buffer_len,
                                           size_t *cursor,
                                           size_t *value_out) {
    if (*cursor >= buffer_len) {
        return RESP_PARSE_INCOMPLETE;
    }

    if (buffer[*cursor] < '0' || buffer[*cursor] > '9') {
        return RESP_PARSE_MALFORMED;
    }

    size_t value = 0U;
    while (*cursor < buffer_len && buffer[*cursor] >= '0' && buffer[*cursor] <= '9') {
        const unsigned digit = (unsigned) (buffer[*cursor] - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            return RESP_PARSE_TOO_LARGE;
        }
        value = value * 10U + digit;
        ++(*cursor);
    }

    if (*cursor >= buffer_len) {
        return RESP_PARSE_INCOMPLETE;
    }
    if (buffer[*cursor] != '\r') {
        return RESP_PARSE_MALFORMED;
    }
    if (*cursor + 1U >= buffer_len) {
        return RESP_PARSE_INCOMPLETE;
    }
    if (buffer[*cursor + 1U] != '\n') {
        return RESP_PARSE_MALFORMED;
    }

    *cursor += 2U;
    *value_out = value;
    return RESP_PARSE_OK;
}

static RespParseResult parse_array_count(const char *buffer,
                                         size_t buffer_len,
                                         size_t *cursor,
                                         size_t *count_out) {
    if (*cursor >= buffer_len) {
        return RESP_PARSE_INCOMPLETE;
    }
    if (buffer[*cursor] != '*') {
        return RESP_PARSE_MALFORMED;
    }

    ++(*cursor);
    RespParseResult result = parse_unsigned_line(buffer, buffer_len, cursor, count_out);
    if (result != RESP_PARSE_OK) {
        return result;
    }
    if (*count_out == 0U || *count_out > RESP_MAX_ARGS) {
        return RESP_PARSE_TOO_LARGE;
    }
    return RESP_PARSE_OK;
}

static RespParseResult parse_bulk_length(const char *buffer,
                                         size_t buffer_len,
                                         size_t *cursor,
                                         size_t *length_out) {
    if (*cursor >= buffer_len) {
        return RESP_PARSE_INCOMPLETE;
    }
    if (buffer[*cursor] != '$') {
        return RESP_PARSE_MALFORMED;
    }

    ++(*cursor);
    if (*cursor < buffer_len && buffer[*cursor] == '-') {
        return RESP_PARSE_MALFORMED;
    }

    RespParseResult result = parse_unsigned_line(buffer, buffer_len, cursor, length_out);
    if (result != RESP_PARSE_OK) {
        return result;
    }
    if (*length_out > RESP_MAX_BULK_SIZE) {
        return RESP_PARSE_TOO_LARGE;
    }
    return RESP_PARSE_OK;
}

static RespParseResult parse_bulk_string(const char *buffer,
                                         size_t buffer_len,
                                         size_t *cursor,
                                         char **argument_out) {
    size_t bulk_length = 0U;
    RespParseResult result = parse_bulk_length(buffer, buffer_len, cursor, &bulk_length);
    if (result != RESP_PARSE_OK) {
        return result;
    }

    if (*cursor > SIZE_MAX - 2U || bulk_length > SIZE_MAX - *cursor - 2U) {
        return RESP_PARSE_TOO_LARGE;
    }
    if (*cursor + bulk_length + 2U > RESP_MAX_COMMAND_SIZE) {
        return RESP_PARSE_TOO_LARGE;
    }
    if (*cursor + bulk_length + 2U > buffer_len) {
        return RESP_PARSE_INCOMPLETE;
    }
    if (!has_crlf_at(buffer, buffer_len, *cursor + bulk_length)) {
        return RESP_PARSE_MALFORMED;
    }

    if (memchr(buffer + *cursor, '\0', bulk_length) != NULL) {
        return RESP_PARSE_MALFORMED;
    }

    char *argument = malloc(bulk_length + 1U);
    if (argument == NULL) {
        return RESP_PARSE_OUT_OF_MEMORY;
    }

    memcpy(argument, buffer + *cursor, bulk_length);
    argument[bulk_length] = '\0';
    *cursor += bulk_length + 2U;
    *argument_out = argument;
    return RESP_PARSE_OK;
}

void command_destroy(Command *command) {
    if (command == NULL) {
        return;
    }

    for (size_t i = 0U; i < command->argc; ++i) {
        free(command->argv[i]);
    }
    free(command->argv);
    free(command);
}

RespParseResult resp_parse_command(const char *buffer,
                                   size_t buffer_len,
                                   Command **command_out,
                                   size_t *bytes_consumed) {
    if (command_out == NULL || bytes_consumed == NULL) {
        return RESP_PARSE_INVALID_ARGUMENT;
    }

    *command_out = NULL;
    *bytes_consumed = 0U;

    if (buffer == NULL) {
        return RESP_PARSE_INVALID_ARGUMENT;
    }
    if (buffer_len == 0U) {
        return RESP_PARSE_INCOMPLETE;
    }

    size_t cursor = 0U;
    size_t argc = 0U;
    RespParseResult result = parse_array_count(buffer, buffer_len, &cursor, &argc);
    if (result != RESP_PARSE_OK) {
        return result;
    }

    Command *command = calloc(1U, sizeof(*command));
    if (command == NULL) {
        return RESP_PARSE_OUT_OF_MEMORY;
    }

    command->argv = calloc(argc, sizeof(*command->argv));
    if (command->argv == NULL) {
        free(command);
        return RESP_PARSE_OUT_OF_MEMORY;
    }
    command->argc = argc;

    for (size_t i = 0U; i < argc; ++i) {
        if (cursor > RESP_MAX_COMMAND_SIZE) {
            result = RESP_PARSE_TOO_LARGE;
            goto fail;
        }
        result = parse_bulk_string(buffer, buffer_len, &cursor, &command->argv[i]);
        if (result != RESP_PARSE_OK) {
            goto fail;
        }
    }

    if (cursor > RESP_MAX_COMMAND_SIZE) {
        result = RESP_PARSE_TOO_LARGE;
        goto fail;
    }

    *command_out = command;
    *bytes_consumed = cursor;
    return RESP_PARSE_OK;

fail:
    command_destroy(command);
    return result;
}

RespReply resp_reply_simple_string(const char *data) {
    RespReply reply;
    reply.type = RESP_REPLY_SIMPLE_STRING;
    reply.data = data;
    reply.length = data == NULL ? 0U : strlen(data);
    reply.integer = 0;
    return reply;
}

RespReply resp_reply_error(const char *message) {
    RespReply reply;
    reply.type = RESP_REPLY_ERROR;
    reply.data = message;
    reply.length = message == NULL ? 0U : strlen(message);
    reply.integer = 0;
    return reply;
}

RespReply resp_reply_integer(long long value) {
    RespReply reply;
    reply.type = RESP_REPLY_INTEGER;
    reply.data = NULL;
    reply.length = 0U;
    reply.integer = value;
    return reply;
}

RespReply resp_reply_bulk_string(const char *data, size_t length) {
    RespReply reply;
    reply.type = RESP_REPLY_BULK_STRING;
    reply.data = data;
    reply.length = length;
    reply.integer = 0;
    return reply;
}

RespReply resp_reply_null_bulk_string(void) {
    RespReply reply;
    reply.type = RESP_REPLY_NULL_BULK_STRING;
    reply.data = NULL;
    reply.length = 0U;
    reply.integer = 0;
    return reply;
}

static RespSerializeResult write_text_reply(char prefix,
                                            const char *text,
                                            char *buffer,
                                            size_t buffer_len,
                                            size_t *written_out) {
    if (text == NULL) {
        return RESP_SERIALIZE_INVALID_ARGUMENT;
    }

    const int needed = snprintf(buffer, buffer_len, "%c%s\r\n", prefix, text);
    if (needed < 0) {
        return RESP_SERIALIZE_INVALID_ARGUMENT;
    }
    if ((size_t) needed >= buffer_len) {
        return RESP_SERIALIZE_BUFFER_TOO_SMALL;
    }

    *written_out = (size_t) needed;
    return RESP_SERIALIZE_OK;
}

RespSerializeResult resp_serialize_reply(const RespReply *reply,
                                         char *buffer,
                                         size_t buffer_len,
                                         size_t *written_out) {
    if (reply == NULL || buffer == NULL || written_out == NULL || buffer_len == 0U) {
        return RESP_SERIALIZE_INVALID_ARGUMENT;
    }

    *written_out = 0U;

    switch (reply->type) {
        case RESP_REPLY_SIMPLE_STRING:
            return write_text_reply('+', reply->data, buffer, buffer_len, written_out);

        case RESP_REPLY_ERROR:
            return write_text_reply('-', reply->data, buffer, buffer_len, written_out);

        case RESP_REPLY_INTEGER: {
            const int needed = snprintf(buffer, buffer_len, ":%lld\r\n", reply->integer);
            if (needed < 0) {
                return RESP_SERIALIZE_INVALID_ARGUMENT;
            }
            if ((size_t) needed >= buffer_len) {
                return RESP_SERIALIZE_BUFFER_TOO_SMALL;
            }
            *written_out = (size_t) needed;
            return RESP_SERIALIZE_OK;
        }

        case RESP_REPLY_NULL_BULK_STRING: {
            const char null_bulk[] = "$-1\r\n";
            const size_t needed = sizeof(null_bulk) - 1U;
            if (needed >= buffer_len) {
                return RESP_SERIALIZE_BUFFER_TOO_SMALL;
            }
            memcpy(buffer, null_bulk, needed + 1U);
            *written_out = needed;
            return RESP_SERIALIZE_OK;
        }

        case RESP_REPLY_BULK_STRING: {
            if (reply->data == NULL) {
                return RESP_SERIALIZE_INVALID_ARGUMENT;
            }

            char header[64];
            const int header_len = snprintf(header, sizeof(header), "$%zu\r\n", reply->length);
            if (header_len < 0 || (size_t) header_len >= sizeof(header)) {
                return RESP_SERIALIZE_INVALID_ARGUMENT;
            }

            const size_t total = (size_t) header_len + reply->length + 2U;
            if (total >= buffer_len) {
                return RESP_SERIALIZE_BUFFER_TOO_SMALL;
            }

            memcpy(buffer, header, (size_t) header_len);
            memcpy(buffer + header_len, reply->data, reply->length);
            memcpy(buffer + header_len + reply->length, "\r\n", 2U);
            buffer[total] = '\0';
            *written_out = total;
            return RESP_SERIALIZE_OK;
        }

        default:
            return RESP_SERIALIZE_INVALID_ARGUMENT;
    }
}

const char *resp_parse_result_string(RespParseResult result) {
    switch (result) {
        case RESP_PARSE_OK: return "RESP_PARSE_OK";
        case RESP_PARSE_INCOMPLETE: return "RESP_PARSE_INCOMPLETE";
        case RESP_PARSE_MALFORMED: return "RESP_PARSE_MALFORMED";
        case RESP_PARSE_TOO_LARGE: return "RESP_PARSE_TOO_LARGE";
        case RESP_PARSE_OUT_OF_MEMORY: return "RESP_PARSE_OUT_OF_MEMORY";
        case RESP_PARSE_INVALID_ARGUMENT: return "RESP_PARSE_INVALID_ARGUMENT";
        default: return "RESP_PARSE_UNKNOWN_RESULT";
    }
}

const char *resp_serialize_result_string(RespSerializeResult result) {
    switch (result) {
        case RESP_SERIALIZE_OK: return "RESP_SERIALIZE_OK";
        case RESP_SERIALIZE_BUFFER_TOO_SMALL: return "RESP_SERIALIZE_BUFFER_TOO_SMALL";
        case RESP_SERIALIZE_INVALID_ARGUMENT: return "RESP_SERIALIZE_INVALID_ARGUMENT";
        default: return "RESP_SERIALIZE_UNKNOWN_RESULT";
    }
}
