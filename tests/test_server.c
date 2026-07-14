#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "store.h"
#include "unity.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

static void set_receive_timeout(int fd) {
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    TEST_ASSERT_EQUAL_INT(0,
                          setsockopt(fd,
                                     SOL_SOCKET,
                                     SO_RCVTIMEO,
                                     &timeout,
                                     (socklen_t) sizeof(timeout)));
}

static void write_all_or_fail(int fd, const char *data, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t written = write(fd, data + offset, length - offset);
        TEST_ASSERT_TRUE(written > 0);
        offset += (size_t) written;
    }
}

static void read_exact_or_fail(int fd, char *buffer, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t received = read(fd, buffer + offset, length - offset);
        TEST_ASSERT_TRUE(received > 0);
        offset += (size_t) received;
    }
}

static pid_t spawn_client_handler(int server_fd,
                                  int peer_fd,
                                  ServerResult expected_result) {
    const pid_t child = fork();
    TEST_ASSERT_TRUE(child >= 0);

    if (child == 0) {
        (void) close(peer_fd);
        Store *store = store_create();
        if (store == NULL) {
            _exit(100);
        }
        const ServerResult result = server_handle_client(server_fd, store);
        store_destroy(store);
        _exit(result == expected_result ? 0 : 101);
    }

    (void) close(server_fd);
    return child;
}

static void assert_child_success(pid_t child) {
    int status = 0;
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

static void test_fragmented_ping_request(void) {
    int sockets[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    set_receive_timeout(sockets[0]);

    const pid_t child = spawn_client_handler(sockets[1], sockets[0], SERVER_OK);

    const char first[] = "*1\r\n$4\r\nPI";
    const char second[] = "NG\r\n";
    write_all_or_fail(sockets[0], first, sizeof(first) - 1U);

    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 20000000L };
    (void) nanosleep(&delay, NULL);

    write_all_or_fail(sockets[0], second, sizeof(second) - 1U);

    char reply[7];
    read_exact_or_fail(sockets[0], reply, sizeof(reply));
    TEST_ASSERT_EQUAL_MEMORY("+PONG\r\n", reply, sizeof(reply));

    (void) shutdown(sockets[0], SHUT_WR);
    (void) close(sockets[0]);
    assert_child_success(child);
}

static void test_two_commands_in_one_write(void) {
    int sockets[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    set_receive_timeout(sockets[0]);

    const pid_t child = spawn_client_handler(sockets[1], sockets[0], SERVER_OK);

    const char requests[] =
        "*1\r\n$4\r\nPING\r\n"
        "*1\r\n$4\r\nPING\r\n";
    write_all_or_fail(sockets[0], requests, sizeof(requests) - 1U);

    char replies[14];
    read_exact_or_fail(sockets[0], replies, sizeof(replies));
    TEST_ASSERT_EQUAL_MEMORY("+PONG\r\n+PONG\r\n", replies, sizeof(replies));

    (void) shutdown(sockets[0], SHUT_WR);
    (void) close(sockets[0]);
    assert_child_success(child);
}

static void test_malformed_request_gets_error_and_connection_closes(void) {
    int sockets[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    set_receive_timeout(sockets[0]);

    const pid_t child = spawn_client_handler(
        sockets[1],
        sockets[0],
        SERVER_CLIENT_PROTOCOL_ERROR);

    const char request[] = "*1\r\n$4\r\nPINGxx";
    write_all_or_fail(sockets[0], request, sizeof(request) - 1U);

    const char expected[] = "-ERR protocol error\r\n";
    char reply[sizeof(expected) - 1U];
    read_exact_or_fail(sockets[0], reply, sizeof(reply));
    TEST_ASSERT_EQUAL_MEMORY(expected, reply, sizeof(reply));

    char extra = '\0';
    TEST_ASSERT_EQUAL_INT(0, read(sockets[0], &extra, 1U));

    (void) close(sockets[0]);
    assert_child_success(child);
}

static void test_store_survives_sequential_client_sessions(void) {
    int first[2];
    int second[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, first));
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, second));
    set_receive_timeout(first[0]);
    set_receive_timeout(second[0]);

    const pid_t child = fork();
    TEST_ASSERT_TRUE(child >= 0);

    if (child == 0) {
        (void) close(first[0]);
        (void) close(second[0]);
        Store *store = store_create();
        if (store == NULL) {
            _exit(110);
        }
        const ServerResult first_result = server_handle_client(first[1], store);
        const ServerResult second_result = server_handle_client(second[1], store);
        store_destroy(store);
        _exit(first_result == SERVER_OK && second_result == SERVER_OK ? 0 : 111);
    }

    (void) close(first[1]);
    (void) close(second[1]);

    const char set_request[] =
        "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nDavid\r\n";
    write_all_or_fail(first[0], set_request, sizeof(set_request) - 1U);
    char set_reply[5];
    read_exact_or_fail(first[0], set_reply, sizeof(set_reply));
    TEST_ASSERT_EQUAL_MEMORY("+OK\r\n", set_reply, sizeof(set_reply));
    (void) shutdown(first[0], SHUT_WR);
    (void) close(first[0]);

    const char get_request[] = "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n";
    write_all_or_fail(second[0], get_request, sizeof(get_request) - 1U);
    const char expected[] = "$5\r\nDavid\r\n";
    char get_reply[sizeof(expected) - 1U];
    read_exact_or_fail(second[0], get_reply, sizeof(get_reply));
    TEST_ASSERT_EQUAL_MEMORY(expected, get_reply, sizeof(get_reply));
    (void) shutdown(second[0], SHUT_WR);
    (void) close(second[0]);

    assert_child_success(child);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fragmented_ping_request);
    RUN_TEST(test_two_commands_in_one_write);
    RUN_TEST(test_malformed_request_gets_error_and_connection_closes);
    RUN_TEST(test_store_survives_sequential_client_sessions);
    return UNITY_END();
}
