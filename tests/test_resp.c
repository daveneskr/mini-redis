#include "resp.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void assert_serializes_to(RespReply reply, const char *expected) {
    char buffer[RESP_MAX_COMMAND_SIZE];
    size_t written = 0U;

    TEST_ASSERT_EQUAL(RESP_SERIALIZE_OK,
                      resp_serialize_reply(&reply, buffer, sizeof(buffer), &written));
    TEST_ASSERT_EQUAL_UINT(strlen(expected), written);
    TEST_ASSERT_EQUAL_MEMORY(expected, buffer, written);
}

void test_parse_ping_command(void) {
    const char input[] = "*1\r\n$4\r\nPING\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_OK,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_NOT_NULL(command);
    TEST_ASSERT_EQUAL_UINT(strlen(input), consumed);
    TEST_ASSERT_EQUAL_UINT(1U, command->argc);
    TEST_ASSERT_EQUAL_STRING("PING", command->argv[0]);

    command_destroy(command);
}

void test_parse_set_command(void) {
    const char input[] = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nDavid\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_OK,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_NOT_NULL(command);
    TEST_ASSERT_EQUAL_UINT(strlen(input), consumed);
    TEST_ASSERT_EQUAL_UINT(3U, command->argc);
    TEST_ASSERT_EQUAL_STRING("SET", command->argv[0]);
    TEST_ASSERT_EQUAL_STRING("name", command->argv[1]);
    TEST_ASSERT_EQUAL_STRING("David", command->argv[2]);

    command_destroy(command);
}

void test_parse_consumes_only_first_command_from_buffer(void) {
    const char input[] = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_OK,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_EQUAL_UINT(strlen("*1\r\n$4\r\nPING\r\n"), consumed);
    TEST_ASSERT_EQUAL_STRING("PING", command->argv[0]);

    command_destroy(command);
}

void test_parse_incomplete_bulk_string_returns_incomplete(void) {
    const char input[] = "*1\r\n$4\r\nPIN";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_INCOMPLETE,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_NULL(command);
    TEST_ASSERT_EQUAL_UINT(0U, consumed);
}

void test_parse_plain_text_command_is_malformed(void) {
    const char input[] = "PING\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_MALFORMED,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_NULL(command);
    TEST_ASSERT_EQUAL_UINT(0U, consumed);
}

void test_parse_too_many_arguments_returns_too_large(void) {
    const char input[] = "*17\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_TOO_LARGE,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_NULL(command);
}

void test_parse_oversized_bulk_string_returns_too_large(void) {
    const char input[] = "*1\r\n$4097\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_TOO_LARGE,
                      resp_parse_command(input, strlen(input), &command, &consumed));
    TEST_ASSERT_NULL(command);
}

void test_parse_rejects_embedded_null_bytes(void) {
    const char input[] = "*1\r\n$4\r\nPI\0G\r\n";
    Command *command = NULL;
    size_t consumed = 0U;

    TEST_ASSERT_EQUAL(RESP_PARSE_MALFORMED,
                      resp_parse_command(input, sizeof(input) - 1U, &command, &consumed));
    TEST_ASSERT_NULL(command);
}

void test_serialize_simple_string_reply(void) {
    assert_serializes_to(resp_reply_simple_string("OK"), "+OK\r\n");
}

void test_serialize_error_reply(void) {
    assert_serializes_to(resp_reply_error("ERR unknown command"), "-ERR unknown command\r\n");
}

void test_serialize_integer_reply(void) {
    assert_serializes_to(resp_reply_integer(42), ":42\r\n");
}

void test_serialize_bulk_string_reply(void) {
    assert_serializes_to(resp_reply_bulk_string("David", 5U), "$5\r\nDavid\r\n");
}

void test_serialize_null_bulk_string_reply(void) {
    assert_serializes_to(resp_reply_null_bulk_string(), "$-1\r\n");
}

void test_serialize_small_buffer_returns_buffer_too_small(void) {
    RespReply reply = resp_reply_simple_string("OK");
    char buffer[3];
    size_t written = 99U;

    TEST_ASSERT_EQUAL(RESP_SERIALIZE_BUFFER_TOO_SMALL,
                      resp_serialize_reply(&reply, buffer, sizeof(buffer), &written));
    TEST_ASSERT_EQUAL_UINT(0U, written);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_ping_command);
    RUN_TEST(test_parse_set_command);
    RUN_TEST(test_parse_consumes_only_first_command_from_buffer);
    RUN_TEST(test_parse_incomplete_bulk_string_returns_incomplete);
    RUN_TEST(test_parse_plain_text_command_is_malformed);
    RUN_TEST(test_parse_too_many_arguments_returns_too_large);
    RUN_TEST(test_parse_oversized_bulk_string_returns_too_large);
    RUN_TEST(test_parse_rejects_embedded_null_bytes);
    RUN_TEST(test_serialize_simple_string_reply);
    RUN_TEST(test_serialize_error_reply);
    RUN_TEST(test_serialize_integer_reply);
    RUN_TEST(test_serialize_bulk_string_reply);
    RUN_TEST(test_serialize_null_bulk_string_reply);
    RUN_TEST(test_serialize_small_buffer_returns_buffer_too_small);
    return UNITY_END();
}
