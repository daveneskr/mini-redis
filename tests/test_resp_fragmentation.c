#include "unity.h"

#include "resp.h"

#include <stddef.h>

void setUp(void) {
}

void tearDown(void) {
}

static void assert_all_proper_prefixes_are_incomplete(const char *request,
                                                       size_t request_length) {
    for (size_t length = 0U; length < request_length; ++length) {
        Command *command = NULL;
        size_t consumed = 0U;
        const RespParseResult result = resp_parse_command(
            request,
            length,
            &command,
            &consumed);

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            RESP_PARSE_INCOMPLETE,
            result,
            "a proper prefix of a valid RESP request must be incomplete");
        TEST_ASSERT_NULL(command);
        TEST_ASSERT_EQUAL_size_t(0U, consumed);
    }
}

static void test_every_set_prefix_is_incomplete(void) {
    static const char request[] =
        "*3\r\n"
        "$3\r\nSET\r\n"
        "$3\r\nkey\r\n"
        "$5\r\nvalue\r\n";

    assert_all_proper_prefixes_are_incomplete(
        request,
        sizeof(request) - 1U);
}

static void test_split_between_cr_and_lf_is_incomplete(void) {
    static const char request[] = "*1\r";
    Command *command = NULL;
    size_t consumed = 0U;

    const RespParseResult result = resp_parse_command(
        request,
        sizeof(request) - 1U,
        &command,
        &consumed);

    TEST_ASSERT_EQUAL_INT(RESP_PARSE_INCOMPLETE, result);
    TEST_ASSERT_NULL(command);
    TEST_ASSERT_EQUAL_size_t(0U, consumed);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_set_prefix_is_incomplete);
    RUN_TEST(test_split_between_cr_and_lf_is_incomplete);
    return UNITY_END();
}
