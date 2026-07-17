#include "unity.h"

#include "mini_redis.h"
#include "store.h"

#include <stddef.h>

static Store *store;

void setUp(void) {
    store = store_create();
    TEST_ASSERT_NOT_NULL(store);
}

void tearDown(void) {
    store_destroy(store);
    store = NULL;
}

static void test_small_output_buffer_does_not_execute_del(void) {
    static const unsigned char request[] =
        "*2\r\n"
        "$3\r\nDEL\r\n"
        "$3\r\nkey\r\n";
    unsigned char output[2];
    size_t consumed = 99U;
    size_t output_length = 99U;

    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "key", "value"));

    const MiniRedisProcessResult result = mini_redis_process_one(
        store,
        request,
        sizeof(request) - 1U,
        &consumed,
        output,
        sizeof(output),
        &output_length);

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OUTPUT_TOO_SMALL, result);
    TEST_ASSERT_EQUAL_size_t(0U, consumed);
    TEST_ASSERT_EQUAL_size_t(0U, output_length);
    TEST_ASSERT_TRUE(store_exists(store, "key"));
}

static void test_full_output_buffer_executes_del_once(void) {
    static const unsigned char request[] =
        "*2\r\n"
        "$3\r\nDEL\r\n"
        "$3\r\nkey\r\n";
    unsigned char output[MINI_REDIS_MAX_REPLY_SIZE];
    size_t consumed = 0U;
    size_t output_length = 0U;

    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "key", "value"));

    const MiniRedisProcessResult result = mini_redis_process_one(
        store,
        request,
        sizeof(request) - 1U,
        &consumed,
        output,
        sizeof(output),
        &output_length);

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OK, result);
    TEST_ASSERT_EQUAL_size_t(sizeof(request) - 1U, consumed);
    TEST_ASSERT_EQUAL_MEMORY(":1\r\n", output, 4U);
    TEST_ASSERT_EQUAL_size_t(4U, output_length);
    TEST_ASSERT_FALSE(store_exists(store, "key"));
}


static void test_small_sufficient_output_buffer_still_works(void) {
    static const unsigned char request[] =
        "*1\r\n"
        "$4\r\nPING\r\n";
    unsigned char output[8];
    size_t consumed = 0U;
    size_t output_length = 0U;

    const MiniRedisProcessResult result = mini_redis_process_one(
        store,
        request,
        sizeof(request) - 1U,
        &consumed,
        output,
        sizeof(output),
        &output_length);

    TEST_ASSERT_EQUAL_INT(MINI_REDIS_PROCESS_OK, result);
    TEST_ASSERT_EQUAL_size_t(sizeof(request) - 1U, consumed);
    TEST_ASSERT_EQUAL_size_t(7U, output_length);
    TEST_ASSERT_EQUAL_MEMORY("+PONG\r\n", output, output_length);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_small_output_buffer_does_not_execute_del);
    RUN_TEST(test_small_sufficient_output_buffer_still_works);
    RUN_TEST(test_full_output_buffer_executes_del_once);
    return UNITY_END();
}
