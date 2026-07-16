#include <stdio.h>
#include "mini_redis.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name) {
    fprintf(stderr,
            "Usage: %s [--bind IPv4_ADDRESS] [--port PORT] [--max-clients COUNT] [--once]\n"
            "\n"
            "Defaults:\n"
            "  --bind %s\n"
            "  --port %u\n"
            "  --max-clients %u\n",
            program_name,
            MINI_REDIS_DEFAULT_BIND_ADDRESS,
            (unsigned) MINI_REDIS_DEFAULT_PORT,
            (unsigned) MINI_REDIS_DEFAULT_MAX_CLIENTS);
}

static int parse_port(const char *text, uint16_t *port_out) {
    if (text == NULL || port_out == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0UL || value > 65535UL) {
        return 0;
    }

    *port_out = (uint16_t) value;
    return 1;
}

static int parse_size(const char *text, size_t *value_out) {
    if (text == NULL || value_out == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0UL) {
        return 0;
    }

    *value_out = (size_t) value;
    return 1;
}

int main(int argc, char **argv) {
    MiniRedisConfig config = {
        .bind_address = MINI_REDIS_DEFAULT_BIND_ADDRESS,
        .port = (uint16_t) MINI_REDIS_DEFAULT_PORT,
        .listen_backlog = MINI_REDIS_DEFAULT_BACKLOG,
        .max_clients = MINI_REDIS_DEFAULT_MAX_CLIENTS,
        .continue_after_client_disconnect = true
    };

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--once") == 0) {
            config.continue_after_client_disconnect = false;
            continue;
        }
        if (strcmp(argv[i], "--bind") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "mini_redis: --bind requires an address\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            config.bind_address = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc || !parse_port(argv[i + 1], &config.port)) {
                fprintf(stderr, "mini_redis: --port requires a value from 1 to 65535\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--max-clients") == 0) {
            if (i + 1 >= argc || !parse_size(argv[i + 1], &config.max_clients)) {
                fprintf(stderr, "mini_redis: --max-clients requires a positive integer\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
            ++i;
            continue;
        }

        fprintf(stderr, "mini_redis: unknown argument: %s\n", argv[i]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const MiniRedisResult result = mini_redis_run(&config);
    if (result != MINI_REDIS_OK) {
        fprintf(stderr,
                "mini_redis: server failed: %s\n",
                mini_redis_result_string(result));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

