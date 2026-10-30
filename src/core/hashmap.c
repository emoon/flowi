#include "hashmap.h"
#include "arena.h"
#include "math.h"
#include "string.h"
#include "memory.h"

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 hashmap_hash(const void* key, u64 size) {
    return XXH64(key, size, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u64 next_power_of_2(u64 n) {
    if (n <= 1)
        return 1;
    if ((n & (n - 1)) == 0)
        return n;

    u64 power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void hashmap_init_raw(HashMapBase* self, FlArena* arena, u64 capacity, u64 key_size, u64 value_size, u64 key_offset,
                      u64 value_offset, u64 node_size) {
    self->arena = arena;
    self->capacity = next_power_of_2(capacity); // Ensure power-of-2 for bitwise indexing
    self->count = 0;
    self->key_size = key_size;
    self->value_size = value_size;
    self->key_offset = key_offset;
    self->value_offset = value_offset;
    self->node_size = node_size;
    self->buckets = arena_alloc_array_zero(arena, HashMapBucket, self->capacity);
    self->free_nodes.first = nullptr;
    self->free_nodes.last = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef bool (*KeyEqualsFunc)(const void* node_key, const void* search_key, u64 key_size);
typedef void (*KeyCopyFunc)(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool generic_key_equals(const void* node_key, const void* search_key, u64 key_size) {
    return memory_compare(node_key, search_key, key_size) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void generic_key_copy(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size) {
    (void)self; // POD keys are stored inline; no separate storage needed
    memory_copy(dest_key, key_size, src_key, key_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool string_key_equals(const void* node_key, const void* search_key, u64 key_size) {
    (void)key_size; // Unused for FlString keys
    return fl_string_equals(*(const FlString*)node_key, *(const FlString*)search_key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void string_key_copy(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size) {
    (void)key_size; // Unused for FlString keys
    // A string key may reference bytes in a caller arena whose lifetime is shorter than this map's,
    // so copy them into the map's own arena. fl_string_copy is a no-op for static/literal strings.
    *(FlString*)dest_key = fl_string_copy(self->arena, *(const FlString*)src_key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HashMapNodeBase* hashmap_allocate_node(HashMapBase* self) {
    HashMapNodeBase* node;
    if (self->free_nodes.first != nullptr) {
        // Reuse from free list - node will be reinitialized by caller, no need to zero
        node = self->free_nodes.first;
        sll_queue_pop(self->free_nodes.first, self->free_nodes.last);
    } else {
        node = (HashMapNodeBase*)fl_arena_alloc_raw_zero(self->arena, self->node_size, _Alignof(HashMapNodeBase));
    }
    return node;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hashmap_free_node(HashMapBase* self, HashMapNodeBase* node) {
    node->next = nullptr;
    sll_queue_push(self->free_nodes.first, self->free_nodes.last, node);
    self->count--;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HashMapNodeBase* hashmap_find_node_internal(HashMapBase* self, u64 hash, const void* key, u64 key_size,
                                                   KeyEqualsFunc key_equals) {
    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        if (node->hash == hash) {
            void* node_key = (u8*)node + self->key_offset;
            if (key_equals(node_key, key, key_size)) {
                return node;
            }
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HashMapNodeBase* hashmap_insert_internal(HashMapBase* self, u64 hash, const void* key, const void* value,
                                                u64 key_size, KeyEqualsFunc key_equals, KeyCopyFunc key_copy) {
    HashMapNodeBase* existing = hashmap_find_node_internal(self, hash, key, key_size, key_equals);
    if (existing) {
        void* node_value = (u8*)existing + self->value_offset;
        memory_copy(node_value, self->value_size, value, self->value_size);
        return existing;
    }

    HashMapNodeBase* node = hashmap_allocate_node(self);

    node->hash = hash;
    node->next = nullptr;

    void* node_key = (u8*)node + self->key_offset;
    void* node_value = (u8*)node + self->value_offset;
    key_copy(self, node_key, key, key_size);
    memory_copy(node_value, self->value_size, value, self->value_size);

    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];
    sll_queue_push(bucket->first, bucket->last, node);
    self->count++;

    return node;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool hashmap_remove_internal(HashMapBase* self, u64 hash, const void* key, u64 key_size,
                                    KeyEqualsFunc key_equals) {
    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        if (node->hash == hash) {
            void* node_key = (u8*)node + self->key_offset;
            if (key_equals(node_key, key, key_size)) {
                sll_queue_remove(bucket->first, bucket->last, node);
                hashmap_free_node(self, node);
                return true;
            }
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_insert_raw(HashMapBase* self, u64 hash, const void* key, const void* value) {
    return hashmap_insert_internal(self, hash, key, value, self->key_size, generic_key_equals, generic_key_copy);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_get_raw(HashMapBase* self, u64 hash, const void* key) {
    return hashmap_find_node_internal(self, hash, key, self->key_size, generic_key_equals);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool hashmap_remove_raw(HashMapBase* self, u64 hash, const void* key) {
    return hashmap_remove_internal(self, hash, key, self->key_size, generic_key_equals);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void hashmap_clear_raw(HashMapBase* self) {
    for_count(i, self->capacity) {
        HashMapBucket* bucket = &self->buckets[i];
        if (bucket->first != nullptr) {
            if (self->free_nodes.last != nullptr) {
                self->free_nodes.last->next = bucket->first;
            } else {
                self->free_nodes.first = bucket->first;
            }
            self->free_nodes.last = bucket->last;

            bucket->first = nullptr;
            bucket->last = nullptr;
        }
    }

    self->count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlString support functions

u64 hashmap_hash_string(const FlString str) {
    // Strings of up to 8 bytes are zero-padded into a u64 and hashed with their length as the seed.
    if (str.length <= 8) {
        u64 padded = 0;
        memory_copy(&padded, sizeof(padded), str.data, str.length);

        return XXH64(&padded, sizeof(padded), str.length);
    }

    XXH64_state_t state;
    XXH64_reset(&state, 0);
    XXH64_update(&state, str.data, str.length);
    return XXH64_digest(&state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_insert_string_raw(HashMapBase* self, const FlString* key, const void* value) {
    u64 hash = hashmap_hash_string(*key);
    return hashmap_insert_internal(self, hash, key, value, sizeof(FlString), string_key_equals, string_key_copy);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_get_string_raw(HashMapBase* self, const FlString* key) {
    u64 hash = hashmap_hash_string(*key);
    return hashmap_find_node_internal(self, hash, key, sizeof(FlString), string_key_equals);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool hashmap_remove_string_raw(HashMapBase* self, const FlString* key) {
    u64 hash = hashmap_hash_string(*key);
    return hashmap_remove_internal(self, hash, key, sizeof(FlString), string_key_equals);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optimized functions for u32 keys

static HashMapNodeBase* hashmap_find_node_u32(HashMapBase* self, u64 hash, u32 key) {
    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        if (node->hash == hash) {
            u32* node_key = (u32*)((u8*)node + self->key_offset);
            if (*node_key == key) {
                return node;
            }
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HashMapNodeBase* hashmap_insert_u32_internal(HashMapBase* self, u64 hash, u32 key, const void* value) {
    HashMapNodeBase* existing = hashmap_find_node_u32(self, hash, key);
    if (existing) {
        void* node_value = (u8*)existing + self->value_offset;
        memory_copy(node_value, self->value_size, value, self->value_size);
        return existing;
    }

    HashMapNodeBase* node = hashmap_allocate_node(self);

    node->hash = hash;
    node->next = nullptr;

    u32* node_key = (u32*)((u8*)node + self->key_offset);
    void* node_value = (u8*)node + self->value_offset;
    *node_key = key;
    memory_copy(node_value, self->value_size, value, self->value_size);

    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];
    sll_queue_push(bucket->first, bucket->last, node);
    self->count++;

    return node;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_insert_u32_raw(HashMapBase* self, u64 hash, u32 key, const void* value) {
    return hashmap_insert_u32_internal(self, hash, key, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_get_u32_raw(HashMapBase* self, u64 hash, u32 key) {
    return hashmap_find_node_u32(self, hash, key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool hashmap_remove_u32_raw(HashMapBase* self, u64 hash, u32 key) {
    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        if (node->hash == hash) {
            u32* node_key = (u32*)((u8*)node + self->key_offset);
            if (*node_key == key) {
                sll_queue_remove(bucket->first, bucket->last, node);
                hashmap_free_node(self, node);
                return true;
            }
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optimized functions for u64 keys (no function pointer overhead)

static HashMapNodeBase* hashmap_find_node_u64(HashMapBase* self, u64 hash, u64 key) {
    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        if (node->hash == hash) {
            u64* node_key = (u64*)((u8*)node + self->key_offset);
            if (*node_key == key) {
                return node;
            }
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HashMapNodeBase* hashmap_insert_u64_internal(HashMapBase* self, u64 hash, u64 key, const void* value) {
    HashMapNodeBase* existing = hashmap_find_node_u64(self, hash, key);
    if (existing) {
        void* node_value = (u8*)existing + self->value_offset;
        memory_copy(node_value, self->value_size, value, self->value_size);
        return existing;
    }

    HashMapNodeBase* node = hashmap_allocate_node(self);

    node->hash = hash;
    node->next = nullptr;

    u64* node_key = (u64*)((u8*)node + self->key_offset);
    void* node_value = (u8*)node + self->value_offset;
    *node_key = key;
    memory_copy(node_value, self->value_size, value, self->value_size);

    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];
    sll_queue_push(bucket->first, bucket->last, node);
    self->count++;

    return node;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_insert_u64_raw(HashMapBase* self, u64 hash, u64 key, const void* value) {
    return hashmap_insert_u64_internal(self, hash, key, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HashMapNodeBase* hashmap_get_u64_raw(HashMapBase* self, u64 hash, u64 key) {
    return hashmap_find_node_u64(self, hash, key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool hashmap_remove_u64_raw(HashMapBase* self, u64 hash, u64 key) {
    const u64 bucket_idx = hash & (self->capacity - 1);
    HashMapBucket* bucket = &self->buckets[bucket_idx];

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        if (node->hash == hash) {
            u64* node_key = (u64*)((u8*)node + self->key_offset);
            if (*node_key == key) {
                sll_queue_remove(bucket->first, bucket->last, node);
                hashmap_free_node(self, node);
                return true;
            }
        }
    }
    return false;
}
