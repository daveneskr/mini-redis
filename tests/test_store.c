#include "store.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Store *store;

void setUp(void) {
    store = store_create();
    TEST_ASSERT_NOT_NULL_MESSAGE(store, "store_create should return a valid store");
}

void tearDown(void) {
    store_destroy(store);
    store = NULL;
}

static void test_create_and_destroy(void) {
    TEST_ASSERT_EQUAL_size_t(0U, store_size(store));
    TEST_ASSERT_EQUAL_size_t(STORE_INITIAL_CAPACITY, store_capacity(store));
    TEST_ASSERT_DOUBLE_WITHIN(0.000001, 0.0, store_load_factor(store));

    store_destroy(NULL);
}

static void test_set_and_get_value(void) {
    const char *value = NULL;

    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "language", "C"));
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_get(store, "language", &value));
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_STRING("C", value);
    TEST_ASSERT_EQUAL_size_t(1U, store_size(store));
}

static void test_update_does_not_increase_size(void) {
    const char *value = NULL;

    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "name", "redis"));
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "name", "mini-redis"));
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_get(store, "name", &value));
    TEST_ASSERT_EQUAL_STRING("mini-redis", value);
    TEST_ASSERT_EQUAL_size_t(1U, store_size(store));
}

static void test_missing_key_returns_not_found_and_null_output(void) {
    const char *value = (const char *)1;

    TEST_ASSERT_EQUAL_INT(STORE_NOT_FOUND,
                          store_get(store, "missing", &value));
    TEST_ASSERT_NULL(value);
}

static void test_delete_existing_and_missing_keys(void) {
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "temp", "value"));
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_delete(store, "temp"));
    TEST_ASSERT_FALSE(store_exists(store, "temp"));
    TEST_ASSERT_EQUAL_size_t(0U, store_size(store));
    TEST_ASSERT_EQUAL_INT(STORE_NOT_FOUND, store_delete(store, "temp"));
}

static void test_exists_reports_presence(void) {
    TEST_ASSERT_FALSE(store_exists(store, "x"));
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "x", "1"));
    TEST_ASSERT_TRUE(store_exists(store, "x"));
}

static void test_accepts_empty_value(void) {
    const char *value = NULL;

    TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, "empty", ""));
    TEST_ASSERT_EQUAL_INT(STORE_OK, store_get(store, "empty", &value));
    TEST_ASSERT_EQUAL_STRING("", value);
}

static void test_rejects_invalid_arguments(void) {
    const char *value = NULL;

    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_set(NULL, "key", "value"));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_set(store, NULL, "value"));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_set(store, "", "value"));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_set(store, "key", NULL));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_get(NULL, "key", &value));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_get(store, NULL, &value));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_get(store, "key", NULL));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_delete(NULL, "key"));
    TEST_ASSERT_EQUAL_INT(STORE_INVALID_ARGUMENT,
                          store_delete(store, NULL));
    TEST_ASSERT_FALSE(store_exists(NULL, "key"));
    TEST_ASSERT_FALSE(store_exists(store, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, store_size(NULL));
}

static void test_rejects_oversized_key_and_value(void) {
    char *long_key = malloc(STORE_MAX_KEY_SIZE + 2U);
    char *long_value = malloc(STORE_MAX_VALUE_SIZE + 2U);

    TEST_ASSERT_NOT_NULL(long_key);
    TEST_ASSERT_NOT_NULL(long_value);

    memset(long_key, 'k', STORE_MAX_KEY_SIZE + 1U);
    long_key[STORE_MAX_KEY_SIZE + 1U] = '\0';
    memset(long_value, 'v', STORE_MAX_VALUE_SIZE + 1U);
    long_value[STORE_MAX_VALUE_SIZE + 1U] = '\0';

    TEST_ASSERT_EQUAL_INT(STORE_KEY_TOO_LARGE,
                          store_set(store, long_key, "value"));
    TEST_ASSERT_EQUAL_INT(STORE_VALUE_TOO_LARGE,
                          store_set(store, "key", long_value));
    TEST_ASSERT_EQUAL_size_t(0U, store_size(store));

    free(long_key);
    free(long_value);
}

static void test_resizes_and_preserves_all_entries(void) {
    enum { KEY_COUNT = 1000 };
    const size_t original_capacity = store_capacity(store);
    char key[32];
    char expected[32];

    for (int i = 0; i < KEY_COUNT; ++i) {
        (void)snprintf(key, sizeof(key), "key-%d", i);
        (void)snprintf(expected, sizeof(expected), "value-%d", i);
        TEST_ASSERT_EQUAL_INT(STORE_OK, store_set(store, key, expected));
    }

    TEST_ASSERT_GREATER_THAN_size_t(original_capacity, store_capacity(store));
    TEST_ASSERT_EQUAL_size_t(KEY_COUNT, store_size(store));
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(STORE_MAX_LOAD_FACTOR,
                                     store_load_factor(store));

    for (int i = 0; i < KEY_COUNT; ++i) {
        const char *value = NULL;
        (void)snprintf(key, sizeof(key), "key-%d", i);
        (void)snprintf(expected, sizeof(expected), "value-%d", i);
        TEST_ASSERT_EQUAL_INT(STORE_OK, store_get(store, key, &value));
        TEST_ASSERT_EQUAL_STRING(expected, value);
    }
}

static void test_repeated_insert_delete_cycles_remain_consistent(void) {
    for (int cycle = 0; cycle < 100; ++cycle) {
        TEST_ASSERT_EQUAL_INT(STORE_OK,
                              store_set(store, "cycle", "value"));
        TEST_ASSERT_TRUE(store_exists(store, "cycle"));
        TEST_ASSERT_EQUAL_INT(STORE_OK, store_delete(store, "cycle"));
        TEST_ASSERT_FALSE(store_exists(store, "cycle"));
        TEST_ASSERT_EQUAL_size_t(0U, store_size(store));
    }
}

static void test_result_string_returns_readable_names(void) {
    TEST_ASSERT_EQUAL_STRING("STORE_OK", store_result_string(STORE_OK));
    TEST_ASSERT_EQUAL_STRING("STORE_NOT_FOUND",
                             store_result_string(STORE_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("STORE_UNKNOWN_RESULT",
                             store_result_string((StoreResult)999));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_and_destroy);
    RUN_TEST(test_set_and_get_value);
    RUN_TEST(test_update_does_not_increase_size);
    RUN_TEST(test_missing_key_returns_not_found_and_null_output);
    RUN_TEST(test_delete_existing_and_missing_keys);
    RUN_TEST(test_exists_reports_presence);
    RUN_TEST(test_accepts_empty_value);
    RUN_TEST(test_rejects_invalid_arguments);
    RUN_TEST(test_rejects_oversized_key_and_value);
    RUN_TEST(test_resizes_and_preserves_all_entries);
    RUN_TEST(test_repeated_insert_delete_cycles_remain_consistent);
    RUN_TEST(test_result_string_returns_readable_names);

    return UNITY_END();
}
