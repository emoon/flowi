#include "hashmap.h"
#include "arena.h"
#include "math.h"
#include "string.h"
#include "memory.h"

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

#include <string.h>

u64 hashmap_hash(const void* key, u64 size) {
    return XXH64(key, size, 0);
}

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

void hashmap_init_raw(HashMapBase* self, FlArena* arena, u64 capacity, const HashMapLayout* layout) {
    self->arena = arena;
    self->capacity = next_power_of_2(capacity); // Ensure power-of-2 for bitwise indexing
    self->count = 0;
    self->key_size = layout->key_size;
    self->value_size = layout->value_size;
    self->key_offset = layout->key_offset;
    self->value_offset = layout->value_offset;
    self->node_size = layout->node_size;
    self->buckets = arena_alloc_array_zero(arena, HashMapBucket, self->capacity);
    self->free_nodes.first = nullptr;
    self->free_nodes.last = nullptr;
}

typedef bool (*KeyEqualsFunc)(const void* node_key, const void* search_key, u64 key_size);
typedef void (*KeyCopyFunc)(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size);

static bool generic_key_equals(const void* node_key, const void* search_key, u64 key_size) {
    return memory_compare(node_key, search_key, key_size) == 0;
}

static void generic_key_copy(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size) {
    (void)self; // POD keys are stored inline; no separate storage needed
    memory_copy(dest_key, key_size, src_key, key_size);
}

static bool string_key_equals(const void* node_key, const void* search_key, u64 key_size) {
    (void)key_size; // Unused for FlString keys
    return fl_string_equals(*(const FlString*)node_key, *(const FlString*)search_key);
}

static void string_key_copy(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size) {
    (void)key_size; // Unused for FlString keys
    // A string key may reference bytes in a caller arena whose lifetime is shorter than this map's,
    // so copy them into the map's own arena. fl_string_copy is a no-op for static/literal strings.
    *(FlString*)dest_key = fl_string_copy(self->arena, *(const FlString*)src_key);
}

static HashMapNodeBase* hashmap_allocate_node(HashMapBase* self) {
    HashMapNodeBase* node;
    if (self->free_nodes.first != nullptr) {
        // A node from the free list is reinitialized by the caller, so it is not zeroed.
        node = self->free_nodes.first;
        sll_queue_pop(self->free_nodes.first, self->free_nodes.last);
    } else {
        node = (HashMapNodeBase*)fl_arena_alloc_raw_zero(self->arena, self->node_size, _Alignof(HashMapNodeBase));
    }
    return node;
}

static void hashmap_free_node(HashMapBase* self, HashMapNodeBase* node) {
    node->next = nullptr;
    sll_queue_push(self->free_nodes.first, self->free_nodes.last, node);
    self->count--;
}

typedef struct KeyProbe {
    u64 hash;
    const void* key;
    u64 key_size;
    KeyEqualsFunc key_equals;
    KeyCopyFunc key_copy;
} KeyProbe;

static HashMapBucket* hashmap_bucket_for(HashMapBase* self, u64 hash) {
    return &self->buckets[hash & (self->capacity - 1)];
}

static HashMapNodeBase* hashmap_find_node_internal(HashMapBase* self, const KeyProbe* probe) {
    HashMapBucket* bucket = hashmap_bucket_for(self, probe->hash);

    for (HashMapNodeBase* node = bucket->first; node != nullptr; node = node->next) {
        void* node_key = (u8*)node + self->key_offset;
        if (node->hash == probe->hash && probe->key_equals(node_key, probe->key, probe->key_size)) {
            return node;
        }
    }
    return nullptr;
}

static HashMapNodeBase* hashmap_insert_internal(HashMapBase* self, const KeyProbe* probe, const void* value) {
    HashMapNodeBase* existing = hashmap_find_node_internal(self, probe);
    if (existing) {
        void* node_value = (u8*)existing + self->value_offset;
        memory_copy(node_value, self->value_size, value, self->value_size);
        return existing;
    }

    HashMapNodeBase* node = hashmap_allocate_node(self);

    node->hash = probe->hash;
    node->next = nullptr;

    void* node_key = (u8*)node + self->key_offset;
    void* node_value = (u8*)node + self->value_offset;
    probe->key_copy(self, node_key, probe->key, probe->key_size);
    memory_copy(node_value, self->value_size, value, self->value_size);

    HashMapBucket* bucket = hashmap_bucket_for(self, probe->hash);
    sll_queue_push(bucket->first, bucket->last, node);
    self->count++;

    return node;
}

static bool hashmap_remove_internal(HashMapBase* self, const KeyProbe* probe) {
    HashMapBucket* bucket = hashmap_bucket_for(self, probe->hash);
    HashMapNodeBase* node = hashmap_find_node_internal(self, probe);

    if (node == nullptr) {
        return false;
    }

    sll_queue_remove(bucket->first, bucket->last, node);
    hashmap_free_node(self, node);
    return true;
}

static KeyProbe hashmap_probe_generic(HashMapBase* self, u64 hash, const void* key) {
    return (KeyProbe){ hash, key, self->key_size, generic_key_equals, generic_key_copy };
}

static KeyProbe hashmap_probe_string(const FlString* key) {
    return (KeyProbe){ hashmap_hash_string(*key), key, sizeof(FlString), string_key_equals, string_key_copy };
}

static bool u32_key_equals(const void* node_key, const void* search_key, u64 key_size) {
    (void)key_size;
    return *(const u32*)node_key == *(const u32*)search_key;
}

static void u32_key_copy(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size) {
    (void)self;
    (void)key_size;
    *(u32*)dest_key = *(const u32*)src_key;
}

static bool u64_key_equals(const void* node_key, const void* search_key, u64 key_size) {
    (void)key_size;
    return *(const u64*)node_key == *(const u64*)search_key;
}

static void u64_key_copy(HashMapBase* self, void* dest_key, const void* src_key, u64 key_size) {
    (void)self;
    (void)key_size;
    *(u64*)dest_key = *(const u64*)src_key;
}

static KeyProbe hashmap_probe_u32(u64 hash, const u32* key) {
    return (KeyProbe){ hash, key, sizeof(u32), u32_key_equals, u32_key_copy };
}

static KeyProbe hashmap_probe_u64(u64 hash, const u64* key) {
    return (KeyProbe){ hash, key, sizeof(u64), u64_key_equals, u64_key_copy };
}

HashMapNodeBase* hashmap_insert_raw(HashMapBase* self, u64 hash, const void* key, const void* value) {
    KeyProbe probe = hashmap_probe_generic(self, hash, key);
    return hashmap_insert_internal(self, &probe, value);
}

HashMapNodeBase* hashmap_get_raw(HashMapBase* self, u64 hash, const void* key) {
    KeyProbe probe = hashmap_probe_generic(self, hash, key);
    return hashmap_find_node_internal(self, &probe);
}

bool hashmap_remove_raw(HashMapBase* self, u64 hash, const void* key) {
    KeyProbe probe = hashmap_probe_generic(self, hash, key);
    return hashmap_remove_internal(self, &probe);
}

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

HashMapNodeBase* hashmap_insert_string_raw(HashMapBase* self, const FlString* key, const void* value) {
    KeyProbe probe = hashmap_probe_string(key);
    return hashmap_insert_internal(self, &probe, value);
}

HashMapNodeBase* hashmap_get_string_raw(HashMapBase* self, const FlString* key) {
    KeyProbe probe = hashmap_probe_string(key);
    return hashmap_find_node_internal(self, &probe);
}

bool hashmap_remove_string_raw(HashMapBase* self, const FlString* key) {
    KeyProbe probe = hashmap_probe_string(key);
    return hashmap_remove_internal(self, &probe);
}

// Optimized functions for u32 keys

HashMapNodeBase* hashmap_insert_u32_raw(HashMapBase* self, u64 hash, u32 key, const void* value) {
    KeyProbe probe = hashmap_probe_u32(hash, &key);
    return hashmap_insert_internal(self, &probe, value);
}

HashMapNodeBase* hashmap_get_u32_raw(HashMapBase* self, u64 hash, u32 key) {
    KeyProbe probe = hashmap_probe_u32(hash, &key);
    return hashmap_find_node_internal(self, &probe);
}

bool hashmap_remove_u32_raw(HashMapBase* self, u64 hash, u32 key) {
    KeyProbe probe = hashmap_probe_u32(hash, &key);
    return hashmap_remove_internal(self, &probe);
}

// Optimized functions for u64 keys

HashMapNodeBase* hashmap_insert_u64_raw(HashMapBase* self, u64 hash, u64 key, const void* value) {
    KeyProbe probe = hashmap_probe_u64(hash, &key);
    return hashmap_insert_internal(self, &probe, value);
}

HashMapNodeBase* hashmap_get_u64_raw(HashMapBase* self, u64 hash, u64 key) {
    KeyProbe probe = hashmap_probe_u64(hash, &key);
    return hashmap_find_node_internal(self, &probe);
}

bool hashmap_remove_u64_raw(HashMapBase* self, u64 hash, u64 key) {
    KeyProbe probe = hashmap_probe_u64(hash, &key);
    return hashmap_remove_internal(self, &probe);
}
