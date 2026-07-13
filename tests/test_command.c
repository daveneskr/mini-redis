#include "command.h"
#include "unity.h"

#include <string.h>

static Store *store;

void setUp(void) {
    store = store_create();
    TEST_ASSERT_NOT_NULL(store);
}

void tearDown(void) {
    store_destroy(store);
    store = NULL;
}

static Command command_from_args(size_t argc, char **argv) {
    Command command;
    command.argc = argc;
    command.argv = argv;
    return command;
}

static void assert_reply_serializes_to(RespReply reply, const char *expected) {
    char buffer[RESP_MAX_COMMAND_SIZE];
    size_t written = 0U;

    TEST_ASSERT_EQUAL(RESP_SERIALIZE_OK,
                      resp_serialize_reply(&reply, buffer, sizeof(buffer), &written));
    TEST_ASSERT_EQUAL_UINT(strlen(expected), written);
    TEST_ASSERT_EQUAL_MEMORY(expected, buffer, written);
}

void test_ping_without_message_returns_pong(void) {
    char *argv[] = { "PING" };
    Command command = command_from_args(1U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "+PONG\r\n");
}

void test_ping_with_message_returns_bulk_message(void) {
    char *argv[] = { "PING", "hello" };
    Command command = command_from_args(2U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "$5\r\nhello\r\n");
}

void test_echo_returns_bulk_message(void) {
    char *argv[] = { "ECHO", "hello" };
    Command command = command_from_args(2U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "$5\r\nhello\r\n");
}

void test_set_then_get_returns_value(void) {
    char *set_argv[] = { "SET", "name", "David" };
    Command set = command_from_args(3U, set_argv);
    char *get_argv[] = { "GET", "name" };
    Command get = command_from_args(2U, get_argv);

    assert_reply_serializes_to(command_execute(store, &set), "+OK\r\n");
    assert_reply_serializes_to(command_execute(store, &get), "$5\r\nDavid\r\n");
}

void test_set_replaces_existing_value(void) {
    char *first_argv[] = { "SET", "name", "David" };
    Command first = command_from_args(3U, first_argv);
    char *second_argv[] = { "SET", "name", "John" };
    Command second = command_from_args(3U, second_argv);
    char *get_argv[] = { "GET", "name" };
    Command get = command_from_args(2U, get_argv);

    assert_reply_serializes_to(command_execute(store, &first), "+OK\r\n");
    assert_reply_serializes_to(command_execute(store, &second), "+OK\r\n");
    assert_reply_serializes_to(command_execute(store, &get), "$4\r\nJohn\r\n");
}

void test_get_missing_key_returns_null_bulk_string(void) {
    char *argv[] = { "GET", "missing" };
    Command command = command_from_args(2U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "$-1\r\n");
}

void test_del_existing_key_returns_one(void) {
    char *set_argv[] = { "SET", "name", "David" };
    Command set = command_from_args(3U, set_argv);
    char *del_argv[] = { "DEL", "name" };
    Command del = command_from_args(2U, del_argv);
    char *get_argv[] = { "GET", "name" };
    Command get = command_from_args(2U, get_argv);

    assert_reply_serializes_to(command_execute(store, &set), "+OK\r\n");
    assert_reply_serializes_to(command_execute(store, &del), ":1\r\n");
    assert_reply_serializes_to(command_execute(store, &get), "$-1\r\n");
}

void test_del_missing_key_returns_zero(void) {
    char *argv[] = { "DEL", "missing" };
    Command command = command_from_args(2U, argv);

    assert_reply_serializes_to(command_execute(store, &command), ":0\r\n");
}

void test_exists_existing_key_returns_one(void) {
    char *set_argv[] = { "SET", "name", "David" };
    Command set = command_from_args(3U, set_argv);
    char *exists_argv[] = { "EXISTS", "name" };
    Command exists = command_from_args(2U, exists_argv);

    assert_reply_serializes_to(command_execute(store, &set), "+OK\r\n");
    assert_reply_serializes_to(command_execute(store, &exists), ":1\r\n");
}

void test_exists_missing_key_returns_zero(void) {
    char *argv[] = { "EXISTS", "missing" };
    Command command = command_from_args(2U, argv);

    assert_reply_serializes_to(command_execute(store, &command), ":0\r\n");
}

void test_command_names_are_case_insensitive(void) {
    char *set_argv[] = { "set", "name", "David" };
    Command set = command_from_args(3U, set_argv);
    char *get_argv[] = { "gEt", "name" };
    Command get = command_from_args(2U, get_argv);

    assert_reply_serializes_to(command_execute(store, &set), "+OK\r\n");
    assert_reply_serializes_to(command_execute(store, &get), "$5\r\nDavid\r\n");
}

void test_unknown_command_returns_error(void) {
    char *argv[] = { "FLUSHDB" };
    Command command = command_from_args(1U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "-ERR unknown command\r\n");
}

void test_wrong_argument_count_returns_error(void) {
    char *argv[] = { "SET", "name" };
    Command command = command_from_args(2U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "-ERR wrong number of arguments\r\n");
}

void test_empty_command_name_returns_protocol_error(void) {
    char *argv[] = { "" };
    Command command = command_from_args(1U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "-ERR protocol error\r\n");
}

void test_set_empty_key_returns_invalid_argument_error(void) {
    char *argv[] = { "SET", "", "value" };
    Command command = command_from_args(3U, argv);

    assert_reply_serializes_to(command_execute(store, &command), "-ERR invalid argument\r\n");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ping_without_message_returns_pong);
    RUN_TEST(test_ping_with_message_returns_bulk_message);
    RUN_TEST(test_echo_returns_bulk_message);
    RUN_TEST(test_set_then_get_returns_value);
    RUN_TEST(test_set_replaces_existing_value);
    RUN_TEST(test_get_missing_key_returns_null_bulk_string);
    RUN_TEST(test_del_existing_key_returns_one);
    RUN_TEST(test_del_missing_key_returns_zero);
    RUN_TEST(test_exists_existing_key_returns_one);
    RUN_TEST(test_exists_missing_key_returns_zero);
    RUN_TEST(test_command_names_are_case_insensitive);
    RUN_TEST(test_unknown_command_returns_error);
    RUN_TEST(test_wrong_argument_count_returns_error);
    RUN_TEST(test_empty_command_name_returns_protocol_error);
    RUN_TEST(test_set_empty_key_returns_invalid_argument_error);
    return UNITY_END();
}
