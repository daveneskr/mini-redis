#include "unity.h"

#include "client_session.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int make_pipe(int fds[2]) {
    return pipe(fds);
}

static int descriptor_is_closed(int fd) {
    errno = 0;
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

void setUp(void) {}
void tearDown(void) {}

void test_client_session_create_initializes_state(void) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_pipe(fds));

    ClientSession *client = client_session_create(fds[0]);

    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL_INT(fds[0], client_session_socket_fd(client));
    TEST_ASSERT_EQUAL_size_t(0U, client->input_length);
    TEST_ASSERT_EQUAL_size_t(0U, client->output_length);
    TEST_ASSERT_EQUAL_size_t(0U, client->output_offset);
    TEST_ASSERT_EQUAL_UINT64(0ULL, client->commands_processed);
    TEST_ASSERT_FALSE(client->close_requested);
    TEST_ASSERT_FALSE(client_session_has_pending_output(client));
    TEST_ASSERT_FALSE(client_session_input_is_full(client));

    client_session_destroy(client);
    close(fds[1]);
}

void test_client_session_destroy_closes_owned_descriptor(void) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_pipe(fds));

    ClientSession *client = client_session_create(fds[0]);
    TEST_ASSERT_NOT_NULL(client);

    client_session_destroy(client);

    TEST_ASSERT_TRUE(descriptor_is_closed(fds[0]));
    close(fds[1]);
}

void test_client_session_rejects_invalid_descriptor(void) {
    TEST_ASSERT_NULL(client_session_create(-1));
}

void test_client_session_pending_output_helper(void) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_pipe(fds));

    ClientSession *client = client_session_create(fds[0]);
    TEST_ASSERT_NOT_NULL(client);

    client->output_length = 5U;
    client->output_offset = 2U;
    TEST_ASSERT_TRUE(client_session_has_pending_output(client));

    client->output_offset = 5U;
    TEST_ASSERT_FALSE(client_session_has_pending_output(client));

    client_session_destroy(client);
    close(fds[1]);
}

void test_client_session_input_full_helper(void) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_pipe(fds));

    ClientSession *client = client_session_create(fds[0]);
    TEST_ASSERT_NOT_NULL(client);

    client->input_length = RESP_MAX_COMMAND_SIZE - 1U;
    TEST_ASSERT_FALSE(client_session_input_is_full(client));

    client->input_length = RESP_MAX_COMMAND_SIZE;
    TEST_ASSERT_TRUE(client_session_input_is_full(client));

    client_session_destroy(client);
    close(fds[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_client_session_create_initializes_state);
    RUN_TEST(test_client_session_destroy_closes_owned_descriptor);
    RUN_TEST(test_client_session_rejects_invalid_descriptor);
    RUN_TEST(test_client_session_pending_output_helper);
    RUN_TEST(test_client_session_input_full_helper);
    return UNITY_END();
}
