#define _POSIX_C_SOURCE 200809L

#include "unity.h"

#include "mini_redis.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t server_pid;
static uint16_t server_port;

static void set_loopback_address(struct sockaddr_in *address,
                                 uint16_t port) {
    TEST_ASSERT_NOT_NULL(address);

    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);

    TEST_ASSERT_EQUAL_INT(
        1,
        inet_pton(AF_INET, "127.0.0.1", &address->sin_addr));
}

static uint16_t reserve_local_port(void) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);

    struct sockaddr_in address;
    set_loopback_address(&address, 0U);

    TEST_ASSERT_EQUAL_INT(
        0,
        bind(fd,
             (const struct sockaddr *) &address,
             (socklen_t) sizeof(address)));

    socklen_t length = (socklen_t) sizeof(address);
    TEST_ASSERT_EQUAL_INT(
        0,
        getsockname(fd,
                    (struct sockaddr *) &address,
                    &length));

    const uint16_t port = ntohs(address.sin_port);

    TEST_ASSERT_EQUAL_INT(0, close(fd));
    return port;
}

static int connect_with_retry(void) {
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 20000000L
    };

    for (int attempt = 0; attempt < 250; ++attempt) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            (void) nanosleep(&delay, NULL);
            continue;
        }

        struct sockaddr_in address;
        set_loopback_address(&address, server_port);

        if (connect(fd,
                    (const struct sockaddr *) &address,
                    (socklen_t) sizeof(address)) == 0) {
            return fd;
        }

        (void) close(fd);
        (void) nanosleep(&delay, NULL);
    }

    return -1;
}

static void write_exact(int fd,
                        const void *buffer,
                        size_t length) {
    const unsigned char *cursor = buffer;
    size_t written = 0U;

    while (written < length) {
        const ssize_t result = write(
            fd,
            cursor + written,
            length - written);

        if (result > 0) {
            written += (size_t) result;
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        TEST_FAIL_MESSAGE("socket write failed");
    }
}

static void read_exact(int fd,
                       const void *expected,
                       size_t length) {
    unsigned char actual[64];

    TEST_ASSERT_TRUE(length <= sizeof(actual));

    size_t received = 0U;

    while (received < length) {
        const ssize_t result = read(
            fd,
            actual + received,
            length - received);

        if (result > 0) {
            received += (size_t) result;
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        TEST_FAIL_MESSAGE(
            "socket read failed or peer closed early");
    }

    TEST_ASSERT_EQUAL_MEMORY(expected, actual, length);
}

void setUp(void) {
    server_port = reserve_local_port();

    server_pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, server_pid);

    if (server_pid == 0) {
        const MiniRedisConfig config = {
            .bind_address = MINI_REDIS_DEFAULT_BIND_ADDRESS,
            .port = server_port,
            .listen_backlog = MINI_REDIS_DEFAULT_BACKLOG,
            .max_clients = 8U,
            .continue_after_client_disconnect = true
        };

        const MiniRedisResult result =
            mini_redis_run(&config);

        _exit(
            result == MINI_REDIS_OK
                ? EXIT_SUCCESS
                : EXIT_FAILURE);
    }
}

void tearDown(void) {
    if (server_pid > 0) {
        (void) kill(server_pid, SIGTERM);

        int status = 0;

        while (waitpid(server_pid, &status, 0) < 0) {
            if (errno != EINTR) {
                break;
            }
        }
    }

    server_pid = -1;
}

static void
test_idle_and_fragmented_clients_do_not_block_others(void) {
    const int fragmented = connect_with_retry();
    const int active = connect_with_retry();

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fragmented);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, active);

    static const char first_fragment[] = "*1\r";
    static const char second_fragment[] =
        "\n$4\r\nPING\r\n";
    static const char ping[] =
        "*1\r\n$4\r\nPING\r\n";
    static const char pong[] =
        "+PONG\r\n";

    write_exact(
        fragmented,
        first_fragment,
        sizeof(first_fragment) - 1U);

    write_exact(
        active,
        ping,
        sizeof(ping) - 1U);

    read_exact(
        active,
        pong,
        sizeof(pong) - 1U);

    write_exact(
        fragmented,
        second_fragment,
        sizeof(second_fragment) - 1U);

    read_exact(
        fragmented,
        pong,
        sizeof(pong) - 1U);

    (void) close(fragmented);
    (void) close(active);
}

static void
test_store_is_shared_and_pipeline_order_is_preserved(void) {
    const int writer = connect_with_retry();
    const int reader = connect_with_retry();

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, writer);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, reader);

    static const char set[] =
        "*3\r\n"
        "$3\r\nSET\r\n"
        "$3\r\nkey\r\n"
        "$5\r\nvalue\r\n";

    static const char get[] =
        "*2\r\n"
        "$3\r\nGET\r\n"
        "$3\r\nkey\r\n";

    static const char three_pings[] =
        "*1\r\n$4\r\nPING\r\n"
        "*1\r\n$4\r\nPING\r\n"
        "*1\r\n$4\r\nPING\r\n";

    static const char ok[] =
        "+OK\r\n";

    static const char value[] =
        "$5\r\nvalue\r\n";

    static const char three_pongs[] =
        "+PONG\r\n"
        "+PONG\r\n"
        "+PONG\r\n";

    write_exact(
        writer,
        set,
        sizeof(set) - 1U);

    read_exact(
        writer,
        ok,
        sizeof(ok) - 1U);

    write_exact(
        reader,
        get,
        sizeof(get) - 1U);

    read_exact(
        reader,
        value,
        sizeof(value) - 1U);

    write_exact(
        reader,
        three_pings,
        sizeof(three_pings) - 1U);

    read_exact(
        reader,
        three_pongs,
        sizeof(three_pongs) - 1U);

    (void) close(writer);
    (void) close(reader);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(
        test_idle_and_fragmented_clients_do_not_block_others);

    RUN_TEST(
        test_store_is_shared_and_pipeline_order_is_preserved);

    return UNITY_END();
}

