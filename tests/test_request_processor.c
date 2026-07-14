#include "mini_redis.h"
#include "store.h"
#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static MiniRedisProcessResult process(Store *store,
                                      const char *input,
                                      unsigned char *output,
                                      size_t output_capacity,
                                      size_t *consumed,
                                      size_t *written) {
    return mini_redis_process_one(store,
                                  (const unsigned char *) input,
                                  strlen(input),
                                  consumed,
                                  output,
                                  output_capacity,
                                  written);
}

static void test_ping_pipeline_stage(void) {
    Store *store = store_create();
    TEST_ASSERT_NOT_NULL(store);

    const char input[] = "*1\r\n$4\r\nPING\r\n";
    unsigned char output[MINI_REDIS_MAX_REPLY_SIZE];
    size_t consumed = 0U;
    size_t written = 0U;

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OK,
                          process(store,
                                  input,
                                  output,
                                  sizeof(output),
                                  &consumed,
                                  &written));
    TEST_ASSERT_EQUAL_size_t(sizeof(input) - 1U, consumed);
    TEST_ASSERT_EQUAL_size_t(7U, written);
    TEST_ASSERT_EQUAL_MEMORY("+PONG\r\n", output, written);

    store_destroy(store);
}

static void test_set_then_get_uses_same_store(void) {
    Store *store = store_create();
    TEST_ASSERT_NOT_NULL(store);

    unsigned char output[MINI_REDIS_MAX_REPLY_SIZE];
    size_t consumed = 0U;
    size_t written = 0U;

    const char set_input[] = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nDavid\r\n";
    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OK,
                          process(store,
                                  set_input,
                                  output,
                                  sizeof(output),
                                  &consumed,
                                  &written));
    TEST_ASSERT_EQUAL_MEMORY("+OK\r\n", output, written);

    const char get_input[] = "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n";
    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OK,
                          process(store,
                                  get_input,
                                  output,
                                  sizeof(output),
                                  &consumed,
                                  &written));
    TEST_ASSERT_EQUAL_MEMORY("$5\r\nDavid\r\n", output, written);

    store_destroy(store);
}

static void test_incomplete_input_has_no_output(void) {
    Store *store = store_create();
    TEST_ASSERT_NOT_NULL(store);

    const char input[] = "*1\r\n$4\r\nPI";
    unsigned char output[MINI_REDIS_MAX_REPLY_SIZE];
    size_t consumed = 99U;
    size_t written = 99U;

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_INCOMPLETE,
                          process(store,
                                  input,
                                  output,
                                  sizeof(output),
                                  &consumed,
                                  &written));
    TEST_ASSERT_EQUAL_size_t(0U, consumed);
    TEST_ASSERT_EQUAL_size_t(0U, written);

    store_destroy(store);
}

static void test_malformed_input_serializes_error(void) {
    Store *store = store_create();
    TEST_ASSERT_NOT_NULL(store);

    const char input[] = "*1\r\n$4\r\nPINGxx";
    unsigned char output[MINI_REDIS_MAX_REPLY_SIZE];
    size_t consumed = 0U;
    size_t written = 0U;

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_PROTOCOL_ERROR,
                          process(store,
                                  input,
                                  output,
                                  sizeof(output),
                                  &consumed,
                                  &written));
    TEST_ASSERT_EQUAL_MEMORY("-ERR protocol error\r\n", output, written);

    store_destroy(store);
}

static void test_small_output_buffer_is_reported(void) {
    Store *store = store_create();
    TEST_ASSERT_NOT_NULL(store);

    const char input[] = "*1\r\n$4\r\nPING\r\n";
    unsigned char output[4];
    size_t consumed = 0U;
    size_t written = 0U;

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL,
                          process(store,
                                  input,
                                  output,
                                  sizeof(output),
                                  &consumed,
                                  &written));
    TEST_ASSERT_EQUAL_size_t(0U, consumed);
    TEST_ASSERT_EQUAL_size_t(0U, written);

    store_destroy(store);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ping_pipeline_stage);
    RUN_TEST(test_set_then_get_uses_same_store);
    RUN_TEST(test_incomplete_input_has_no_output);
    RUN_TEST(test_malformed_input_serializes_error);
    RUN_TEST(test_small_output_buffer_is_reported);
    return UNITY_END();
}
