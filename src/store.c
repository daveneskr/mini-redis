#include "store.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FNV_OFFSET_BASIS UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

typedef struct StoreEntry {
    char *key;
    char *value;
    struct StoreEntry *next;
} StoreEntry;

struct Store {
    StoreEntry **buckets;
    size_t capacity;
    size_t size;
};

static uint64_t hash_key(const char *key) {
    uint64_t hash = FNV_OFFSET_BASIS;
    const unsigned char *cursor = (const unsigned char *) key;

    while (*cursor != '\0') {
        hash ^= (uint64_t) *cursor++;
        hash *= FNV_PRIME;
    }
    return hash;
}

static size_t index_for(const char *key, size_t capacity) {
    return (size_t) (hash_key(key) % capacity);
}

static size_t bounded_string_length(const char *string, size_t limit) {
    size_t length = 0U;
    while (length < limit && string[length] != '\0') {
        ++length;
    }
    return length;
}

static char *copy_string(const char *source, size_t length) {
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, source, length + 1U);
    return copy;
}

static StoreResult validate_key(const char *key, size_t *length_out) {
    if (key == NULL) {
        return STORE_INVALID_ARGUMENT;
    }

    const size_t length = bounded_string_length(key, STORE_MAX_KEY_SIZE + 1U);
    if (length == 0U) {
        return STORE_INVALID_ARGUMENT;
    }
    if (length > STORE_MAX_KEY_SIZE) {
        return STORE_KEY_TOO_LARGE;
    }

    if (length_out != NULL) {
        *length_out = length;
    }
    return STORE_OK;
}

static StoreResult validate_value(const char *value, size_t *length_out) {
    if (value == NULL) {
        return STORE_INVALID_ARGUMENT;
    }

    const size_t length = bounded_string_length(value, STORE_MAX_VALUE_SIZE + 1U);
    if (length > STORE_MAX_VALUE_SIZE) {
        return STORE_VALUE_TOO_LARGE;
    }

    if (length_out != NULL) {
        *length_out = length;
    }
    return STORE_OK;
}

static StoreEntry *find_entry(const Store *store, const char *key,
                              size_t bucket_index) {
    for (StoreEntry *entry = store->buckets[bucket_index];
         entry != NULL;
         entry = entry->next) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
    }
    return NULL;
}

static bool capacity_would_overflow(size_t capacity) {
    return capacity > SIZE_MAX / 2U || capacity * 2U > SIZE_MAX / sizeof(StoreEntry *);
}

static StoreResult resize_store(Store *store, size_t new_capacity) {
    StoreEntry **new_buckets = calloc(new_capacity, sizeof(*new_buckets));
    if (new_buckets == NULL) {
        return STORE_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < store->capacity; ++i) {
        StoreEntry *entry = store->buckets[i];
        while (entry != NULL) {
            StoreEntry *next = entry->next;
            const size_t new_index = index_for(entry->key, new_capacity);
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;
            entry = next;
        }
    }

    free(store->buckets);
    store->buckets = new_buckets;
    store->capacity = new_capacity;
    return STORE_OK;
}

static StoreResult ensure_capacity_for_insert(Store *store) {
    const double projected_load =
        (double) (store->size + 1U) / (double) store->capacity;

    if (projected_load <= STORE_MAX_LOAD_FACTOR) {
        return STORE_OK;
    }
    if (capacity_would_overflow(store->capacity)) {
        return STORE_OUT_OF_MEMORY;
    }
    return resize_store(store, store->capacity * 2U);
}

Store *store_create(void) {
    Store *store = malloc(sizeof(*store));
    if (store == NULL) {
        return NULL;
    }

    store->buckets = calloc(STORE_INITIAL_CAPACITY, sizeof(*store->buckets));
    if (store->buckets == NULL) {
        free(store);
        return NULL;
    }

    store->capacity = STORE_INITIAL_CAPACITY;
    store->size = 0U;
    return store;
}

void store_destroy(Store *store) {
    if (store == NULL) {
        return;
    }

    for (size_t i = 0; i < store->capacity; ++i) {
        StoreEntry *entry = store->buckets[i];
        while (entry != NULL) {
            StoreEntry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }

    free(store->buckets);
    free(store);
}

StoreResult store_set(Store *store, const char *key, const char *value) {
    if (store == NULL) {
        return STORE_INVALID_ARGUMENT;
    }

    size_t key_length = 0U;
    size_t value_length = 0U;
    StoreResult result = validate_key(key, &key_length);
    if (result != STORE_OK) {
        return result;
    }
    result = validate_value(value, &value_length);
    if (result != STORE_OK) {
        return result;
    }

    const size_t bucket = index_for(key, store->capacity);
    StoreEntry *existing = find_entry(store, key, bucket);
    if (existing != NULL) {
        char *new_value = copy_string(value, value_length);
        if (new_value == NULL) {
            return STORE_OUT_OF_MEMORY;
        }
        free(existing->value);
        existing->value = new_value;
        return STORE_OK;
    }

    result = ensure_capacity_for_insert(store);
    if (result != STORE_OK) {
        return result;
    }

    char *key_copy = copy_string(key, key_length);
    if (key_copy == NULL) {
        return STORE_OUT_OF_MEMORY;
    }
    char *value_copy = copy_string(value, value_length);
    if (value_copy == NULL) {
        free(key_copy);
        return STORE_OUT_OF_MEMORY;
    }
    StoreEntry *entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        free(key_copy);
        free(value_copy);
        return STORE_OUT_OF_MEMORY;
    }

    const size_t insertion_bucket = index_for(key, store->capacity);
    entry->key = key_copy;
    entry->value = value_copy;
    entry->next = store->buckets[insertion_bucket];
    store->buckets[insertion_bucket] = entry;
    ++store->size;
    return STORE_OK;
}

StoreResult store_get(const Store *store, const char *key,
                      const char **value_out) {
    if (store == NULL || value_out == NULL) {
        return STORE_INVALID_ARGUMENT;
    }
    *value_out = NULL;

    const StoreResult validation = validate_key(key, NULL);
    if (validation != STORE_OK) {
        return validation;
    }

    const size_t bucket = index_for(key, store->capacity);
    StoreEntry *entry = find_entry(store, key, bucket);
    if (entry == NULL) {
        return STORE_NOT_FOUND;
    }

    *value_out = entry->value;
    return STORE_OK;
}

StoreResult store_delete(Store *store, const char *key) {
    if (store == NULL) {
        return STORE_INVALID_ARGUMENT;
    }

    const StoreResult validation = validate_key(key, NULL);
    if (validation != STORE_OK) {
        return validation;
    }

    const size_t bucket = index_for(key, store->capacity);
    StoreEntry *previous = NULL;
    StoreEntry *entry = store->buckets[bucket];

    while (entry != NULL) {
        if (strcmp(entry->key, key) == 0) {
            if (previous == NULL) {
                store->buckets[bucket] = entry->next;
            } else {
                previous->next = entry->next;
            }
            free(entry->key);
            free(entry->value);
            free(entry);
            --store->size;
            return STORE_OK;
        }
        previous = entry;
        entry = entry->next;
    }

    return STORE_NOT_FOUND;
}

bool store_exists(const Store *store, const char *key) {
    if (store == NULL || validate_key(key, NULL) != STORE_OK) {
        return false;
    }
    const size_t bucket = index_for(key, store->capacity);
    return find_entry(store, key, bucket) != NULL;
}

size_t store_size(const Store *store) {
    return store == NULL ? 0U : store->size;
}

size_t store_capacity(const Store *store) {
    return store == NULL ? 0U : store->capacity;
}

double store_load_factor(const Store *store) {
    if (store == NULL || store->capacity == 0U) {
        return 0.0;
    }
    return (double) store->size / (double) store->capacity;
}

const char *store_result_string(StoreResult result) {
    switch (result) {
        case STORE_OK: return "STORE_OK";
        case STORE_NOT_FOUND: return "STORE_NOT_FOUND";
        case STORE_INVALID_ARGUMENT: return "STORE_INVALID_ARGUMENT";
        case STORE_OUT_OF_MEMORY: return "STORE_OUT_OF_MEMORY";
        case STORE_KEY_TOO_LARGE: return "STORE_KEY_TOO_LARGE";
        case STORE_VALUE_TOO_LARGE: return "STORE_VALUE_TOO_LARGE";
        default: return "STORE_UNKNOWN_RESULT";
    }
}
