#include "command.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef RespReply (*CommandHandler)(Store *store, const Command *command);
typedef size_t (*CommandCapacity)(const Store *store, const Command *command);

typedef struct CommandSpec {
    const char *name;
    size_t min_argc;
    size_t max_argc;
    CommandHandler handler;
    CommandCapacity capacity;
} CommandSpec;


static size_t decimal_digits(size_t value) {
    size_t digits = 1U;
    while (value >= 10U) {
        value /= 10U;
        ++digits;
    }
    return digits;
}

static size_t text_reply_capacity(const char *text) {
    return text == NULL ? 0U : strlen(text) + 4U;
}

static size_t bulk_reply_capacity(size_t length) {
    return length + decimal_digits(length) + 6U;
}

static size_t max_size(size_t left, size_t right) {
    return left > right ? left : right;
}

static size_t protocol_error_capacity(void) {
    return text_reply_capacity("ERR protocol error");
}

static size_t wrong_arity_capacity(void) {
    return text_reply_capacity("ERR wrong number of arguments");
}

static size_t store_fixed_reply_capacity(void) {
    size_t required = text_reply_capacity("OK");
    required = max_size(required, text_reply_capacity("ERR key too large"));
    required = max_size(required, text_reply_capacity("ERR value too large"));
    required = max_size(required, text_reply_capacity("ERR out of memory"));
    required = max_size(required, text_reply_capacity("ERR invalid argument"));
    required = max_size(required, text_reply_capacity("ERR store operation failed"));
    required = max_size(required, 6U); /* null bulk reply */
    required = max_size(required, 5U); /* :0 or :1 plus terminating NUL */
    return required;
}

static size_t ping_capacity(const Store *store, const Command *command) {
    (void) store;
    if (command->argc == 1U) {
        return text_reply_capacity("PONG");
    }
    return bulk_reply_capacity(strlen(command->argv[1]));
}

static size_t echo_capacity(const Store *store, const Command *command) {
    (void) store;
    return bulk_reply_capacity(strlen(command->argv[1]));
}

static size_t set_capacity(const Store *store, const Command *command) {
    (void) store;
    (void) command;
    return store_fixed_reply_capacity();
}

static size_t get_capacity(const Store *store, const Command *command) {
    size_t required = store_fixed_reply_capacity();
    if (store == NULL) {
        return required;
    }

    const char *value = NULL;
    if (store_get(store, command->argv[1], &value) == STORE_OK && value != NULL) {
        required = max_size(required, bulk_reply_capacity(strlen(value)));
    }
    return required;
}

static size_t integer_store_capacity(const Store *store,
                                     const Command *command) {
    (void) store;
    (void) command;
    return store_fixed_reply_capacity();
}

static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char) (c - ('a' - 'A'));
    }
    return c;
}

static bool command_name_equals(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        return false;
    }

    while (*left != '\0' && *right != '\0') {
        if (ascii_upper(*left) != ascii_upper(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static RespReply store_error_reply(StoreResult result) {
    switch (result) {
        case STORE_KEY_TOO_LARGE:
            return resp_reply_error("ERR key too large");
        case STORE_VALUE_TOO_LARGE:
            return resp_reply_error("ERR value too large");
        case STORE_OUT_OF_MEMORY:
            return resp_reply_error("ERR out of memory");
        case STORE_INVALID_ARGUMENT:
            return resp_reply_error("ERR invalid argument");
        case STORE_NOT_FOUND:
            return resp_reply_null_bulk_string();
        case STORE_OK:
            return resp_reply_simple_string("OK");
        default:
            return resp_reply_error("ERR store operation failed");
    }
}

static RespReply handle_ping(Store *store, const Command *command) {
    (void) store;

    if (command->argc == 1U) {
        return resp_reply_simple_string("PONG");
    }
    return resp_reply_bulk_string(command->argv[1], strlen(command->argv[1]));
}

static RespReply handle_echo(Store *store, const Command *command) {
    (void) store;
    return resp_reply_bulk_string(command->argv[1], strlen(command->argv[1]));
}

static RespReply handle_set(Store *store, const Command *command) {
    const StoreResult result = store_set(store, command->argv[1], command->argv[2]);
    if (result == STORE_OK) {
        return resp_reply_simple_string("OK");
    }
    return store_error_reply(result);
}

static RespReply handle_get(Store *store, const Command *command) {
    const char *value = NULL;
    const StoreResult result = store_get(store, command->argv[1], &value);

    if (result == STORE_OK) {
        return resp_reply_bulk_string(value, strlen(value));
    }
    if (result == STORE_NOT_FOUND) {
        return resp_reply_null_bulk_string();
    }
    return store_error_reply(result);
}

static RespReply handle_del(Store *store, const Command *command) {
    const StoreResult result = store_delete(store, command->argv[1]);

    if (result == STORE_OK) {
        return resp_reply_integer(1);
    }
    if (result == STORE_NOT_FOUND) {
        return resp_reply_integer(0);
    }
    return store_error_reply(result);
}

static RespReply handle_exists(Store *store, const Command *command) {
    const char *value = NULL;
    const StoreResult result = store_get(store, command->argv[1], &value);
    (void) value;

    if (result == STORE_OK) {
        return resp_reply_integer(1);
    }
    if (result == STORE_NOT_FOUND) {
        return resp_reply_integer(0);
    }
    return store_error_reply(result);
}

static const CommandSpec COMMANDS[] = {
    { "PING", 1U, 2U, handle_ping, ping_capacity },
    { "ECHO", 2U, 2U, handle_echo, echo_capacity },
    { "SET", 3U, 3U, handle_set, set_capacity },
    { "GET", 2U, 2U, handle_get, get_capacity },
    { "DEL", 2U, 2U, handle_del, integer_store_capacity },
    { "EXISTS", 2U, 2U, handle_exists, integer_store_capacity }
};

size_t command_reply_capacity_required(const Store *store,
                                       const Command *command) {
    if (command == NULL || command->argc == 0U || command->argv == NULL ||
        command->argv[0] == NULL || command->argv[0][0] == '\0') {
        return protocol_error_capacity();
    }

    const size_t command_count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);
    for (size_t i = 0U; i < command_count; ++i) {
        const CommandSpec *spec = &COMMANDS[i];
        if (!command_name_equals(command->argv[0], spec->name)) {
            continue;
        }
        if (command->argc < spec->min_argc || command->argc > spec->max_argc) {
            return wrong_arity_capacity();
        }
        return spec->capacity(store, command);
    }

    return text_reply_capacity("ERR unknown command");
}

RespReply command_execute(Store *store, const Command *command) {
    if (command == NULL || command->argc == 0U || command->argv == NULL || command->argv[0] == NULL || command->argv[0][0] == '\0') {
        return resp_reply_error("ERR protocol error");
    }

    const size_t command_count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);
    for (size_t i = 0U; i < command_count; ++i) {
        const CommandSpec *spec = &COMMANDS[i];
        if (command_name_equals(command->argv[0], spec->name)) {
            if (command->argc < spec->min_argc || command->argc > spec->max_argc) {
                return resp_reply_error("ERR wrong number of arguments");
            }
            return spec->handler(store, command);
        }
    }

    return resp_reply_error("ERR unknown command");
}
