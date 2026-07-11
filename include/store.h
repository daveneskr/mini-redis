#ifndef MINI_REDIS_STORE_H
#define MINI_REDIS_STORE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_INITIAL_CAPACITY 16U
#define STORE_MAX_KEY_SIZE 512U
#define STORE_MAX_VALUE_SIZE 4096U
#define STORE_MAX_LOAD_FACTOR 0.75

    typedef struct Store Store;

    typedef enum StoreResult {
        STORE_OK = 0,
        STORE_NOT_FOUND,
        STORE_INVALID_ARGUMENT,
        STORE_OUT_OF_MEMORY,
        STORE_KEY_TOO_LARGE,
        STORE_VALUE_TOO_LARGE
    } StoreResult;

    /** Allocates an empty store. Returns NULL if allocation fails. */
    Store *store_create(void);

    /** Frees the store and all keys and values owned by it. Safe for NULL. */
    void store_destroy(Store *store);

    /**
     * Inserts a key/value pair or replaces the value of an existing key.
     * The store copies both strings and owns those copies.
     */
    StoreResult store_set(Store *store, const char *key, const char *value);

    /**
     * Returns a read-only pointer to the stored value in value_out.
     * The pointer remains valid until that key is updated or deleted, or until
     * the store is destroyed. The caller must not modify or free it.
     */
    StoreResult store_get(const Store *store, const char *key,
                          const char **value_out);

    /** Deletes a key/value pair and frees its storage. */
    StoreResult store_delete(Store *store, const char *key);

    /** Returns false for an invalid store or key. */
    bool store_exists(const Store *store, const char *key);

    /** Returns zero for NULL. */
    size_t store_size(const Store *store);

    /** Returns zero for NULL. */
    size_t store_capacity(const Store *store);

    /** Returns zero for NULL or a zero-capacity store. */
    double store_load_factor(const Store *store);

    /** Human-readable name for a result code. */
    const char *store_result_string(StoreResult result);

#ifdef __cplusplus
}
#endif

#endif
