#define _POSIX_C_SOURCE 200809L

#include "unity.h"

#include "server.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_nonblocking_preserves_existing_flags(void) {
    int sockets[2] = { -1, -1 };
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));

    const int before = fcntl(sockets[0], F_GETFL, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, before);

    TEST_ASSERT_EQUAL_INT(SERVER_OK, server_set_nonblocking(sockets[0]));

    const int after = fcntl(sockets[0], F_GETFL, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, after);
    TEST_ASSERT_BITS_HIGH(O_NONBLOCK, after);
    TEST_ASSERT_EQUAL_INT(before, after & ~O_NONBLOCK);

    (void) close(sockets[0]);
    (void) close(sockets[1]);
}

static void test_nonblocking_rejects_invalid_descriptor(void) {
    TEST_ASSERT_EQUAL_INT(SERVER_INVALID_ARGUMENT, server_set_nonblocking(-1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nonblocking_preserves_existing_flags);
    RUN_TEST(test_nonblocking_rejects_invalid_descriptor);
    return UNITY_END();
}
