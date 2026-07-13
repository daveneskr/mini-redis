#ifndef MINI_REDIS_RESP_H
#define MINI_REDIS_RESP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RESP_MAX_ARGS 16U
#define RESP_MAX_BULK_SIZE 4096U
#define RESP_MAX_COMMAND_SIZE 8192U

typedef struct Command {
    size_t argc;
    char **argv;
} Command;

typedef enum RespParseResult {
    RESP_PARSE_OK = 0,
    RESP_PARSE_INCOMPLETE,
    RESP_PARSE_MALFORMED,
    RESP_PARSE_TOO_LARGE,
    RESP_PARSE_OUT_OF_MEMORY,
    RESP_PARSE_INVALID_ARGUMENT
} RespParseResult;

typedef enum RespReplyType {
    RESP_REPLY_SIMPLE_STRING = 0,
    RESP_REPLY_ERROR,
    RESP_REPLY_INTEGER,
    RESP_REPLY_BULK_STRING,
    RESP_REPLY_NULL_BULK_STRING
} RespReplyType;

typedef struct RespReply {
    RespReplyType type;
    const char *data;
    size_t length;
    long long integer;
} RespReply;

typedef enum RespSerializeResult {
    RESP_SERIALIZE_OK = 0,
    RESP_SERIALIZE_BUFFER_TOO_SMALL,
    RESP_SERIALIZE_INVALID_ARGUMENT
} RespSerializeResult;

/**
 * Parses one RESP2 command frame from buffer.
 *
 * Supported request format: arrays of bulk strings, for example:
 * *2\r\n$3\r\nGET\r\n$4\r\nname\r\n
 *
 * On RESP_PARSE_OK, command_out owns the returned Command and bytes_consumed
 * contains the number of bytes consumed from buffer.
 */
RespParseResult resp_parse_command(const char *buffer,
                                   size_t buffer_len,
                                   Command **command_out,
                                   size_t *bytes_consumed);

/** Frees a Command returned by resp_parse_command. Safe for NULL. */
void command_destroy(Command *command);

RespReply resp_reply_simple_string(const char *data);
RespReply resp_reply_error(const char *message);
RespReply resp_reply_integer(long long value);
RespReply resp_reply_bulk_string(const char *data, size_t length);
RespReply resp_reply_null_bulk_string(void);

/** Serializes a RespReply into buffer. */
RespSerializeResult resp_serialize_reply(const RespReply *reply,
                                         char *buffer,
                                         size_t buffer_len,
                                         size_t *written_out);

const char *resp_parse_result_string(RespParseResult result);
const char *resp_serialize_result_string(RespSerializeResult result);

#ifdef __cplusplus
}
#endif

#endif
