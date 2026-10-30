#pragma once

#include "core.h"
#include "math.h"
#include "string.h"
#include "types.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct HashMapNodeBase {
    struct HashMapNodeBase* next;
    u64 hash;
    u8 data[];
} HashMapNodeBase;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct HashMapBucket {
    HashMapNodeBase* first;
    HashMapNodeBase* last;
} HashMapBucket;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct HashMapBase {
    u64 count;
    u64 capacity;
    HashMapBucket* buckets;
    HashMapBucket free_nodes;
    struct FlArena* arena;
    u64 key_size;
    u64 value_size;
    u64 key_offset;
    u64 value_offset;
    u64 node_size;
} HashMapBase;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-safe hashmap declaration using typeof
//
// Usage:
//   hashmap(u32, FlString) my_map;
//   hashmap_new(&my_map, arena, 16);
//   hashmap_insert(&my_map, 42, FL_S("value"));
//   FlString* value = hashmap_get(&my_map, 42);

#define hashmap(key_type, value_type) \
    struct {                          \
        HashMapBase base;             \
        key_type* _key_type;          \
        value_type* _value_type;      \
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize a hashmap. initial_capacity is a bucket count, rounded up to a power of 2.

#define hashmap_new(map, arena_ptr, initial_capacity)                                                              \
    do {                                                                                                           \
        typedef __typeof__(*(map)->_key_type) _key_t;                                                              \
        typedef __typeof__(*(map)->_value_type) _value_t;                                                          \
        /* Nodes are allocated at _Alignof(HashMapNodeBase); more strictly aligned types would be under-aligned */ \
        _Static_assert(_Alignof(_key_t) <= _Alignof(HashMapNodeBase),                                              \
                       "hashmap: key type alignment exceeds node storage alignment");                              \
        _Static_assert(_Alignof(_value_t) <= _Alignof(HashMapNodeBase),                                            \
                       "hashmap: value type alignment exceeds node storage alignment");                            \
        u64 _key_offset = offsetof(HashMapNodeBase, data);                                                         \
        u64 _value_offset = _key_offset + align_pow2(sizeof(_key_t), _Alignof(_value_t));                          \
        u64 _node_size = _value_offset + sizeof(_value_t);                                                         \
        hashmap_init_raw(&(map)->base, arena_ptr, initial_capacity, sizeof(_key_t), sizeof(_value_t), _key_offset, \
                         _value_offset, _node_size);                                                               \
        (map)->_key_type = nullptr;                                                                                \
        (map)->_value_type = nullptr;                                                                              \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reads a key as an integer of the given type by copying at most sizeof(key) bytes; the rest is zero.

#define hashmap__key_bits(type, key)                                                                 \
    __extension__({                                                                                  \
        type _bits = 0;                                                                              \
        __builtin_memcpy(&_bits, &(key), sizeof(_bits) < sizeof(key) ? sizeof(_bits) : sizeof(key)); \
        _bits;                                                                                       \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Insert a key-value pair; evaluates to a pointer to the inserted value (nullptr if allocation failed)

#define hashmap_insert(map, key_val, value_val)                                                         \
    ({                                                                                                  \
        typedef __typeof__(*(map)->_key_type) _key_t;                                                   \
        typedef __typeof__(*(map)->_value_type) _value_t;                                               \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),                       \
                       "hashmap_insert: key type mismatch");                                            \
        _Static_assert(__builtin_types_compatible_p(__typeof__(value_val), _value_t),                   \
                       "hashmap_insert: value type mismatch");                                          \
        _key_t _key = key_val;                                                                          \
        _value_t _value = value_val;                                                                    \
        HashMapNodeBase* _node = nullptr;                                                               \
        if (sizeof(_key_t) == sizeof(u32) && __builtin_types_compatible_p(_key_t, u32)) {               \
            u64 _hash = hashmap_hash_u32_xxhash(hashmap__key_bits(u32, _key));                          \
            _node = hashmap_insert_u32_raw(&(map)->base, _hash, hashmap__key_bits(u32, _key), &_value); \
        } else if (sizeof(_key_t) == sizeof(u64) && __builtin_types_compatible_p(_key_t, u64)) {        \
            u64 _hash = hashmap_hash_u64_xxhash(hashmap__key_bits(u64, _key));                          \
            _node = hashmap_insert_u64_raw(&(map)->base, _hash, hashmap__key_bits(u64, _key), &_value); \
        } else {                                                                                        \
            u64 _hash = hashmap_hash_optimized(_key, sizeof(_key_t));                                   \
            _node = hashmap_insert_raw(&(map)->base, _hash, &_key, &_value);                            \
        }                                                                                               \
        _node ? (_value_t*)((u8*)_node + (map)->base.value_offset) : nullptr;                           \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a value by key; evaluates to a pointer to the value (nullptr if the key is not present)

#define hashmap_get(map, key_val)                                                                                    \
    ({                                                                                                               \
        typedef __typeof__(*(map)->_key_type) _key_t;                                                                \
        typedef __typeof__(*(map)->_value_type) _value_t;                                                            \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t), "hashmap_get: key type mismatch"); \
        _key_t _key = key_val;                                                                                       \
        const HashMapBase* _base = &(map)->base; /* const-correct */                                                 \
        HashMapNodeBase* _node = nullptr;                                                                            \
        if (sizeof(_key_t) == sizeof(u32) && __builtin_types_compatible_p(_key_t, u32)) {                            \
            u64 _hash = hashmap_hash_u32_xxhash(hashmap__key_bits(u32, _key));                                       \
            _node = hashmap_get_u32_raw((HashMapBase*)_base, _hash, hashmap__key_bits(u32, _key));                   \
        } else if (sizeof(_key_t) == sizeof(u64) && __builtin_types_compatible_p(_key_t, u64)) {                     \
            u64 _hash = hashmap_hash_u64_xxhash(hashmap__key_bits(u64, _key));                                       \
            _node = hashmap_get_u64_raw((HashMapBase*)_base, _hash, hashmap__key_bits(u64, _key));                   \
        } else {                                                                                                     \
            u64 _hash = hashmap_hash_optimized(_key, sizeof(_key_t));                                                \
            _node = hashmap_get_raw((HashMapBase*)_base, _hash, &_key);                                              \
        }                                                                                                            \
        _node ? (_value_t*)((u8*)_node + (map)->base.value_offset) : nullptr;                                        \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Remove a key-value pair; evaluates to true if the key was found and removed

#define hashmap_remove(map, key_val)                                                             \
    ({                                                                                           \
        typedef __typeof__(*(map)->_key_type) _key_t;                                            \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),                \
                       "hashmap_remove: key type mismatch");                                     \
        _key_t _key = key_val;                                                                   \
        bool _result = false;                                                                    \
        if (sizeof(_key_t) == sizeof(u32) && __builtin_types_compatible_p(_key_t, u32)) {        \
            u64 _hash = hashmap_hash_u32_xxhash(hashmap__key_bits(u32, _key));                   \
            _result = hashmap_remove_u32_raw(&(map)->base, _hash, hashmap__key_bits(u32, _key)); \
        } else if (sizeof(_key_t) == sizeof(u64) && __builtin_types_compatible_p(_key_t, u64)) { \
            u64 _hash = hashmap_hash_u64_xxhash(hashmap__key_bits(u64, _key));                   \
            _result = hashmap_remove_u64_raw(&(map)->base, _hash, hashmap__key_bits(u64, _key)); \
        } else {                                                                                 \
            u64 _hash = hashmap_hash_optimized(_key, sizeof(_key_t));                            \
            _result = hashmap_remove_raw(&(map)->base, _hash, &_key);                            \
        }                                                                                        \
        _result;                                                                                 \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define hashmap_contains(map, key_val)                                                                          \
    ({                                                                                                          \
        typedef __typeof__(*(map)->_key_type) _key_t;                                                           \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),                               \
                       "hashmap_contains: key type mismatch");                                                  \
        _key_t _key = key_val;                                                                                  \
        const HashMapBase* _base = &(map)->base;                                                                \
        bool _result = false;                                                                                   \
        if (sizeof(_key_t) == sizeof(u32) && __builtin_types_compatible_p(_key_t, u32)) {                       \
            u64 _hash = hashmap_hash_u32_xxhash(hashmap__key_bits(u32, _key));                                  \
            _result = hashmap_get_u32_raw((HashMapBase*)_base, _hash, hashmap__key_bits(u32, _key)) != nullptr; \
        } else if (sizeof(_key_t) == sizeof(u64) && __builtin_types_compatible_p(_key_t, u64)) {                \
            u64 _hash = hashmap_hash_u64_xxhash(hashmap__key_bits(u64, _key));                                  \
            _result = hashmap_get_u64_raw((HashMapBase*)_base, _hash, hashmap__key_bits(u64, _key)) != nullptr; \
        } else {                                                                                                \
            u64 _hash = hashmap_hash_optimized(_key, sizeof(_key_t));                                           \
            _result = hashmap_get_raw((HashMapBase*)_base, _hash, &_key) != nullptr;                            \
        }                                                                                                       \
        _result;                                                                                                \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Iterate over all key-value pairs; key_var and value_var are pointers to the stored data

#define _HASHMAP_CONCAT_IMPL(x, y) x##y
#define _HASHMAP_CONCAT(x, y) _HASHMAP_CONCAT_IMPL(x, y)
#define _HASHMAP_UNIQUE(prefix) _HASHMAP_CONCAT(prefix, __LINE__)

#define for_each_hashmap(map, key_var, value_var)                                                               \
    for (u64 _HASHMAP_UNIQUE(_i_) = 0; _HASHMAP_UNIQUE(_i_) < (map)->base.capacity; _HASHMAP_UNIQUE(_i_)++)     \
        for (HashMapNodeBase * _HASHMAP_UNIQUE(_node_) = (map)->base.buckets[_HASHMAP_UNIQUE(_i_)].first;       \
             _HASHMAP_UNIQUE(_node_) != nullptr; _HASHMAP_UNIQUE(_node_) = _HASHMAP_UNIQUE(_node_)->next)       \
            for (int _HASHMAP_UNIQUE(_once_) = 1; _HASHMAP_UNIQUE(_once_); _HASHMAP_UNIQUE(_once_) = 0)         \
                for (__typeof__(*(map)->_key_type)* key_var                                                     \
                     = (__typeof__(*(map)->_key_type)*)((u8*)_HASHMAP_UNIQUE(_node_) + (map)->base.key_offset); \
                     _HASHMAP_UNIQUE(_once_); _HASHMAP_UNIQUE(_once_) = 0)                                      \
                    for (__typeof__(*(map)->_value_type)* value_var                                             \
                         = (__typeof__(*(map)->_value_type)*)((u8*)_HASHMAP_UNIQUE(_node_)                      \
                                                              + (map)->base.value_offset);                      \
                         _HASHMAP_UNIQUE(_once_); _HASHMAP_UNIQUE(_once_) = 0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define hashmap_count(map) ((map)->base.count)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear all entries (keeps memory allocated, resets count to 0)

#define hashmap_clear(map) hashmap_clear_raw(&(map)->base)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ROTL32(x, r) ((x << r) | (x >> (32 - r)))
#define ROTL64(x, r) ((x << r) | (x >> (64 - r)))

static inline u32 hashmap_hash_u32_xxhash(u32 key) {
    const u32 PRIME2 = 2246822519U;
    const u32 PRIME3 = 3266489917U;
    const u32 PRIME4 = 668265263U;
    const u32 PRIME5 = 374761393U;

    u32 h32 = PRIME5 + 4; // seed + len
    h32 += key * PRIME3;
    h32 = ROTL32(h32, 17) * PRIME4;

    h32 ^= h32 >> 15;
    h32 *= PRIME2;
    h32 ^= h32 >> 13;
    h32 *= PRIME3;
    h32 ^= h32 >> 16;

    return h32;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u64 hashmap_hash_u64_xxhash(u64 key) {
    const u64 PRIME1 = 11400714785074694791ULL;
    const u64 PRIME2 = 14029467366897019727ULL;
    const u64 PRIME3 = 1609587929392839161ULL;
    const u64 PRIME5 = 2870177450012600261ULL;

    u64 h64 = PRIME5 + 8; // seed + len
    h64 += key * PRIME2;
    h64 = ROTL64(h64, 31) * PRIME1;

    h64 ^= h64 >> 33;
    h64 *= PRIME2;
    h64 ^= h64 >> 29;
    h64 *= PRIME3;
    h64 ^= h64 >> 32;

    return h64;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hash selection by key size: 4-byte and 8-byte keys use the integer hashes, pointers hash as u64, anything else
// hashes its bytes.

#define hashmap_hash_optimized(key_val, key_size)                                                               \
    ((key_size) == sizeof(u32) && !__builtin_types_compatible_p(__typeof__(key_val), void*)                     \
         ? (u64)hashmap_hash_u32_xxhash(hashmap__key_bits(u32, key_val))                                        \
     : (key_size) == sizeof(u64) && !__builtin_types_compatible_p(__typeof__(key_val), void*)                   \
         ? hashmap_hash_u64_xxhash(hashmap__key_bits(u64, key_val))                                             \
     : __builtin_types_compatible_p(__typeof__(key_val), void*) ? hashmap_hash_u64_xxhash((uintptr_t)(key_val)) \
                                                                : hashmap_hash(&(key_val), (key_size)))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// FlString support

struct FlString;
u64 hashmap_hash_string(const struct FlString str);
HashMapNodeBase* hashmap_insert_string_raw(HashMapBase* self, const struct FlString* key, const void* value);
HashMapNodeBase* hashmap_get_string_raw(HashMapBase* self, const struct FlString* key);
bool hashmap_remove_string_raw(HashMapBase* self, const struct FlString* key);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlString-specific hashmap macros (optimized for string keys)

#define hashmap_string_new(map, arena_ptr, initial_capacity) hashmap_new(map, arena_ptr, initial_capacity)

#define hashmap_string_insert(map, key_val, value_val)                                    \
    ({                                                                                    \
        typedef __typeof__(*(map)->_key_type) _key_t;                                     \
        typedef __typeof__(*(map)->_value_type) _value_t;                                 \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),         \
                       "hashmap_string_insert: key type mismatch (expected FlString)");   \
        _Static_assert(__builtin_types_compatible_p(__typeof__(value_val), _value_t),     \
                       "hashmap_string_insert: value type mismatch");                     \
        _key_t _key = key_val;                                                            \
        _value_t _value = value_val;                                                      \
        HashMapNodeBase* _node = hashmap_insert_string_raw(&(map)->base, &_key, &_value); \
        _node ? (_value_t*)((u8*)_node + (map)->base.value_offset) : nullptr;             \
    })

#define hashmap_string_get(map, key_val)                                             \
    ({                                                                               \
        typedef __typeof__(*(map)->_key_type) _key_t;                                \
        typedef __typeof__(*(map)->_value_type) _value_t;                            \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),    \
                       "hashmap_string_get: key type mismatch (expected FlString)"); \
        _key_t _key = key_val;                                                       \
        const HashMapBase* _base = &(map)->base;                                     \
        HashMapNodeBase* _node = hashmap_get_string_raw((HashMapBase*)_base, &_key); \
        _node ? (_value_t*)((u8*)_node + (map)->base.value_offset) : nullptr;        \
    })

#define hashmap_string_remove(map, key_val)                                             \
    ({                                                                                  \
        typedef __typeof__(*(map)->_key_type) _key_t;                                   \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),       \
                       "hashmap_string_remove: key type mismatch (expected FlString)"); \
        _key_t _key = key_val;                                                          \
        hashmap_remove_string_raw(&(map)->base, &_key);                                 \
    })

#define hashmap_string_contains(map, key_val)                                             \
    ({                                                                                    \
        typedef __typeof__(*(map)->_key_type) _key_t;                                     \
        _Static_assert(__builtin_types_compatible_p(__typeof__(key_val), _key_t),         \
                       "hashmap_string_contains: key type mismatch (expected FlString)"); \
        _key_t _key = key_val;                                                            \
        const HashMapBase* _base = &(map)->base;                                          \
        hashmap_get_string_raw((HashMapBase*)_base, &_key) != nullptr;                    \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raw interface (used by macros)

void hashmap_init_raw(HashMapBase* self, struct FlArena* arena, u64 capacity, u64 key_size, u64 value_size,
                      u64 key_offset, u64 value_offset, u64 node_size);
HashMapNodeBase* hashmap_insert_raw(HashMapBase* self, u64 hash, const void* key, const void* value);
HashMapNodeBase* hashmap_get_raw(HashMapBase* self, u64 hash, const void* key);
bool hashmap_remove_raw(HashMapBase* self, u64 hash, const void* key);
void hashmap_clear_raw(HashMapBase* self);
u64 hashmap_hash(const void* key, u64 size);

// Optimized functions for specific key types (no function pointer overhead)
HashMapNodeBase* hashmap_insert_u32_raw(HashMapBase* self, u64 hash, u32 key, const void* value);
HashMapNodeBase* hashmap_get_u32_raw(HashMapBase* self, u64 hash, u32 key);
bool hashmap_remove_u32_raw(HashMapBase* self, u64 hash, u32 key);

HashMapNodeBase* hashmap_insert_u64_raw(HashMapBase* self, u64 hash, u64 key, const void* value);
HashMapNodeBase* hashmap_get_u64_raw(HashMapBase* self, u64 hash, u64 key);
bool hashmap_remove_u64_raw(HashMapBase* self, u64 hash, u64 key);
