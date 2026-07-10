#include "unity.h"
#include "mini_redis.h"

void setUp(void) {
    // Runs before each test
}

void tearDown(void) {
    // Runs after each test
}

void test_add_two_positive_numbers(void) {
    TEST_ASSERT_EQUAL_INT(5, mini_redis_add(2, 3));
}

void test_add_negative_and_positive_number(void) {
    TEST_ASSERT_EQUAL_INT(0, mini_redis_add(-1, 1));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_add_two_positive_numbers);
    RUN_TEST(test_add_negative_and_positive_number);

    return UNITY_END();
}
