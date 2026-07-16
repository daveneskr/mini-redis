#include "unity.h"

#include "client_registry.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int descriptor_is_closed(int fd) {
    errno = 0;
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

static ClientSession *make_session(int *peer_fd_out) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(fds));
    if (peer_fd_out != NULL) {
        *peer_fd_out = fds[1];
    } else {
        close(fds[1]);
    }
    return client_session_create(fds[0]);
}

void setUp(void) {}
void tearDown(void) {}

void test_client_registry_create_initializes_empty_registry(void) {
    ClientRegistry *registry = client_registry_create(3U);

    TEST_ASSERT_NOT_NULL(registry);
    TEST_ASSERT_EQUAL_size_t(0U, client_registry_count(registry));
    TEST_ASSERT_EQUAL_size_t(3U, client_registry_capacity(registry));
    TEST_ASSERT_NULL(client_registry_get(registry, 0U));

    client_registry_destroy(registry);
}

void test_client_registry_rejects_zero_capacity(void) {
    TEST_ASSERT_NULL(client_registry_create(0U));
}

void test_client_registry_adds_and_retrieves_clients(void) {
    int peer_a = -1;
    int peer_b = -1;
    ClientRegistry *registry = client_registry_create(2U);
    ClientSession *client_a = make_session(&peer_a);
    ClientSession *client_b = make_session(&peer_b);

    TEST_ASSERT_NOT_NULL(registry);
    TEST_ASSERT_NOT_NULL(client_a);
    TEST_ASSERT_NOT_NULL(client_b);

    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client_a));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client_b));

    TEST_ASSERT_EQUAL_size_t(2U, client_registry_count(registry));
    TEST_ASSERT_EQUAL_PTR(client_a, client_registry_get(registry, 0U));
    TEST_ASSERT_EQUAL_PTR(client_b, client_registry_get(registry, 1U));
    TEST_ASSERT_EQUAL_PTR(client_a, client_registry_find_by_fd(registry, client_session_socket_fd(client_a)));

    client_registry_destroy(registry);
    close(peer_a);
    close(peer_b);
}

void test_client_registry_enforces_capacity_without_taking_ownership_on_failure(void) {
    int peer_a = -1;
    int peer_b = -1;
    ClientRegistry *registry = client_registry_create(1U);
    ClientSession *client_a = make_session(&peer_a);
    ClientSession *client_b = make_session(&peer_b);
    const int client_b_fd = client_session_socket_fd(client_b);

    TEST_ASSERT_NOT_NULL(registry);
    TEST_ASSERT_NOT_NULL(client_a);
    TEST_ASSERT_NOT_NULL(client_b);

    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client_a));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_LIMIT_REACHED, client_registry_add(registry, client_b));
    TEST_ASSERT_EQUAL_size_t(1U, client_registry_count(registry));
    TEST_ASSERT_FALSE(descriptor_is_closed(client_b_fd));

    client_session_destroy(client_b);
    TEST_ASSERT_TRUE(descriptor_is_closed(client_b_fd));

    client_registry_destroy(registry);
    close(peer_a);
    close(peer_b);
}

void test_client_registry_rejects_duplicate_descriptor(void) {
    int peer = -1;
    ClientRegistry *registry = client_registry_create(2U);
    ClientSession *client = make_session(&peer);

    TEST_ASSERT_NOT_NULL(registry);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_DUPLICATE_DESCRIPTOR, client_registry_add(registry, client));
    TEST_ASSERT_EQUAL_size_t(1U, client_registry_count(registry));

    client_registry_destroy(registry);
    close(peer);
}

void test_client_registry_remove_at_destroys_removed_client_and_preserves_others(void) {
    int peer_a = -1;
    int peer_b = -1;
    int peer_c = -1;
    ClientRegistry *registry = client_registry_create(3U);
    ClientSession *client_a = make_session(&peer_a);
    ClientSession *client_b = make_session(&peer_b);
    ClientSession *client_c = make_session(&peer_c);
    const int client_b_fd = client_session_socket_fd(client_b);

    TEST_ASSERT_NOT_NULL(registry);
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client_a));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client_b));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client_c));

    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_remove_at(registry, 1U));

    TEST_ASSERT_TRUE(descriptor_is_closed(client_b_fd));
    TEST_ASSERT_EQUAL_size_t(2U, client_registry_count(registry));
    TEST_ASSERT_NOT_NULL(client_registry_find_by_fd(registry, client_session_socket_fd(client_a)));
    TEST_ASSERT_NOT_NULL(client_registry_find_by_fd(registry, client_session_socket_fd(client_c)));

    client_registry_destroy(registry);
    close(peer_a);
    close(peer_b);
    close(peer_c);
}

void test_client_registry_remove_by_fd(void) {
    int peer = -1;
    ClientRegistry *registry = client_registry_create(1U);
    ClientSession *client = make_session(&peer);
    const int client_fd = client_session_socket_fd(client);

    TEST_ASSERT_NOT_NULL(registry);
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_add(registry, client));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_OK, client_registry_remove_by_fd(registry, client_fd));
    TEST_ASSERT_TRUE(descriptor_is_closed(client_fd));
    TEST_ASSERT_EQUAL_size_t(0U, client_registry_count(registry));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_NOT_FOUND, client_registry_remove_by_fd(registry, client_fd));

    client_registry_destroy(registry);
    close(peer);
}

void test_client_registry_invalid_arguments_are_safe(void) {
    ClientRegistry *registry = client_registry_create(1U);

    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_INVALID_ARGUMENT, client_registry_add(NULL, NULL));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_INVALID_ARGUMENT, client_registry_add(registry, NULL));
    TEST_ASSERT_EQUAL_INT(CLIENT_REGISTRY_NOT_FOUND, client_registry_remove_at(registry, 0U));
    TEST_ASSERT_EQUAL_size_t(0U, client_registry_count(NULL));
    TEST_ASSERT_EQUAL_size_t(0U, client_registry_capacity(NULL));
    TEST_ASSERT_NULL(client_registry_get(NULL, 0U));
    TEST_ASSERT_NULL(client_registry_find_by_fd(NULL, 1));

    client_registry_destroy(registry);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_client_registry_create_initializes_empty_registry);
    RUN_TEST(test_client_registry_rejects_zero_capacity);
    RUN_TEST(test_client_registry_adds_and_retrieves_clients);
    RUN_TEST(test_client_registry_enforces_capacity_without_taking_ownership_on_failure);
    RUN_TEST(test_client_registry_rejects_duplicate_descriptor);
    RUN_TEST(test_client_registry_remove_at_destroys_removed_client_and_preserves_others);
    RUN_TEST(test_client_registry_remove_by_fd);
    RUN_TEST(test_client_registry_invalid_arguments_are_safe);
    return UNITY_END();
}
