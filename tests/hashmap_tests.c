#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/hashmap.h>
#include <core/string.h>
#include <string.h>

typedef struct {
    int data;
} HashmapTestStruct;
typedef struct {
    char data[256];
} LargeStruct;

UTEST(hashmap, int_to_int) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(int, int) map;
    hashmap_new(&map, arena, 16);

    hashmap_insert(&map, 42, 100);
    hashmap_insert(&map, 7, 200);
    hashmap_insert(&map, 99, 300);

    int* val = hashmap_get(&map, 42);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 100);

    val = hashmap_get(&map, 7);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 200);

    val = hashmap_get(&map, 99);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 300);

    val = hashmap_get(&map, 123);
    ASSERT_EQ(val, NULL);

    hashmap_insert(&map, 42, 999);
    val = hashmap_get(&map, 42);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 999);

    ASSERT_EQ(hashmap_count(&map), 3UL);

    ASSERT_TRUE(hashmap_contains(&map, 42));
    ASSERT_TRUE(hashmap_contains(&map, 7));
    ASSERT_TRUE(hashmap_contains(&map, 99));
    ASSERT_FALSE(hashmap_contains(&map, 123));

    ASSERT_TRUE(hashmap_remove(&map, 7));
    ASSERT_EQ(hashmap_count(&map), 2UL);
    ASSERT_FALSE(hashmap_contains(&map, 7));
    ASSERT_FALSE(hashmap_remove(&map, 7));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, int_to_struct) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(int, HashmapTestStruct) map;
    hashmap_new(&map, arena, 8);

    HashmapTestStruct s1 = { .data = 100 };
    HashmapTestStruct s2 = { .data = 200 };
    HashmapTestStruct s3 = { .data = 300 };

    hashmap_insert(&map, 1, s1);
    hashmap_insert(&map, 2, s2);
    hashmap_insert(&map, 3, s3);

    HashmapTestStruct* val = hashmap_get(&map, 2);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(val->data, 200);

    int count = 0;
    int sum_data = 0;
    for_each_hashmap(&map, key, value) {
        count++;
        sum_data += value->data;
        ASSERT_GE(*key, 1);
        ASSERT_LE(*key, 3);
    }
    ASSERT_EQ(count, 3);
    ASSERT_EQ(sum_data, 600);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, pointer_keys) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(void*, int) map;
    hashmap_new(&map, arena, 16);

    int obj1 = 0, obj2 = 0, obj3 = 0;
    void* ptr1 = &obj1;
    void* ptr2 = &obj2;
    void* ptr3 = &obj3;

    hashmap_insert(&map, ptr1, 111);
    hashmap_insert(&map, ptr2, 222);
    hashmap_insert(&map, ptr3, 333);

    int* val = hashmap_get(&map, ptr2);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 222);

    int obj4 = 0;
    void* ptr4 = &obj4;
    val = hashmap_get(&map, ptr4);
    ASSERT_EQ(val, NULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, clear_and_reuse) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(u64, u64) map;
    hashmap_new(&map, arena, 8);

    for (u64 i = 0; i < 10; i++) {
        hashmap_insert(&map, i, i * 100);
    }

    ASSERT_EQ(hashmap_count(&map), 10UL);

    hashmap_clear(&map);
    ASSERT_EQ(hashmap_count(&map), 0UL);

    for (u64 i = 0; i < 10; i++) {
        ASSERT_EQ(hashmap_get(&map, i), NULL);
    }

    // Should be able to insert again (tests free list)
    hashmap_insert(&map, (u64)42, (u64)4200);
    u64* val = hashmap_get(&map, (u64)42);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 4200UL);
    ASSERT_EQ(hashmap_count(&map), 1UL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, collision_handling) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    // Four buckets and no resizing: 64 keys put at least 16 of them in some bucket, whatever the hash does.
    enum { BUCKETS = 4, KEY_COUNT = 64 };
    hashmap(int, int) map;
    hashmap_new(&map, arena, BUCKETS);
    ASSERT_EQ(map.base.capacity, (u64)BUCKETS);

    for (int i = 0; i < KEY_COUNT; i++) {
        hashmap_insert(&map, i, i * 100);
    }
    ASSERT_EQ(hashmap_count(&map), (u64)KEY_COUNT);

    for (int i = 0; i < KEY_COUNT; i++) {
        int* val = hashmap_get(&map, i);
        ASSERT_NE(val, NULL);
        ASSERT_EQ(*val, i * 100);
    }

    // Removing every other key leaves the colliding neighbours reachable.
    for (int i = 0; i < KEY_COUNT; i += 2) {
        hashmap_remove(&map, i);
    }
    ASSERT_EQ(hashmap_count(&map), (u64)(KEY_COUNT / 2));
    for (int i = 0; i < KEY_COUNT; i++) {
        int* val = hashmap_get(&map, i);
        if (i % 2 == 0) {
            ASSERT_EQ(val, NULL);
        } else {
            ASSERT_NE(val, NULL);
            ASSERT_EQ(*val, i * 100);
        }
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, large_data) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(int, LargeStruct) map;
    hashmap_new(&map, arena, 32);

    for (int i = 0; i < 20; i++) {
        LargeStruct s;
        memset(s.data, 'A' + i, sizeof(s.data));
        hashmap_insert(&map, i, s);
    }

    for (int i = 0; i < 20; i++) {
        LargeStruct* val = hashmap_get(&map, i);
        ASSERT_NE(val, NULL);
        ASSERT_EQ((int)val->data[0], 'A' + i);
    }

    ASSERT_EQ(hashmap_count(&map), 20UL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 16);

    FlString key1 = FL_S("hello");
    FlString key2 = FL_S("world");
    FlString key3 = FL_S("test");

    hashmap_string_insert(&map, key1, 100);
    hashmap_string_insert(&map, key2, 200);
    hashmap_string_insert(&map, key3, 300);

    int* val = hashmap_string_get(&map, key1);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 100);

    val = hashmap_string_get(&map, key2);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 200);

    val = hashmap_string_get(&map, key3);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 300);

    ASSERT_EQ(hashmap_count(&map), 3UL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_same_content_different_memory) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 16);

    char* buf1 = arena_alloc_array(arena, char, 5);
    char* buf2 = arena_alloc_array(arena, char, 5);
    memcpy(buf1, "hello", 5);
    memcpy(buf2, "hello", 5);

    FlString key1 = { .data = buf1, .length = 5 };
    FlString key2 = { .data = buf2, .length = 5 };

    ASSERT_NE(key1.data, key2.data);
    ASSERT_TRUE(fl_string_equals(key1, key2));

    hashmap_string_insert(&map, key1, 42);

    int* val = hashmap_string_get(&map, key2);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 42);

    ASSERT_TRUE(hashmap_string_contains(&map, key1));
    ASSERT_TRUE(hashmap_string_contains(&map, key2));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_empty_strings) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 16);

    FlString empty1 = FL_S("");
    FlString empty2 = fl_string_copy(arena, FL_S(""));

    hashmap_string_insert(&map, empty1, 999);

    int* val = hashmap_string_get(&map, empty2);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 999);

    ASSERT_TRUE(hashmap_string_contains(&map, empty1));
    ASSERT_TRUE(hashmap_string_contains(&map, empty2));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_update_value) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 16);

    FlString key = FL_S("updateme");

    hashmap_string_insert(&map, key, 100);
    int* val = hashmap_string_get(&map, key);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 100);

    hashmap_string_insert(&map, key, 200);
    val = hashmap_string_get(&map, key);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 200);

    ASSERT_EQ(hashmap_count(&map), 1UL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_remove) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 16);

    FlString key1 = FL_S("remove1");
    FlString key2 = FL_S("remove2");
    FlString key3 = FL_S("keep");

    hashmap_string_insert(&map, key1, 100);
    hashmap_string_insert(&map, key2, 200);
    hashmap_string_insert(&map, key3, 300);

    ASSERT_EQ(hashmap_count(&map), 3UL);

    ASSERT_TRUE(hashmap_string_remove(&map, key1));
    ASSERT_EQ(hashmap_count(&map), 2UL);
    ASSERT_FALSE(hashmap_string_contains(&map, key1));
    ASSERT_TRUE(hashmap_string_contains(&map, key2));
    ASSERT_TRUE(hashmap_string_contains(&map, key3));

    ASSERT_FALSE(hashmap_string_remove(&map, key1));

    FlString key2_copy = fl_string_copy(arena, FL_S("remove2"));
    ASSERT_TRUE(hashmap_string_remove(&map, key2_copy));
    ASSERT_EQ(hashmap_count(&map), 1UL);
    ASSERT_FALSE(hashmap_string_contains(&map, key2));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_iteration) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 16);

    FlString keys[] = { FL_S("alpha"), FL_S("beta"), FL_S("gamma"), FL_S("delta") };
    int values[] = { 10, 20, 30, 40 };

    for (int i = 0; i < 4; i++) {
        hashmap_string_insert(&map, keys[i], values[i]);
    }

    int count = 0;
    int sum = 0;
    for_each_hashmap(&map, key, value) {
        count++;
        sum += *value;

        bool found = false;
        for (int i = 0; i < 4; i++) {
            if (fl_string_equals(*key, keys[i]) && *value == values[i]) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found);
    }

    ASSERT_EQ(count, 4);
    ASSERT_EQ(sum, 100);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_collision_handling) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    // Small capacity to potentially force collisions
    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 4);

    FlString keys[] = { FL_S("a"),   FL_S("aa"),         FL_S("aaa"),        FL_S("b"),       FL_S("bb"),
                        FL_S("bbb"), FL_S("collision1"), FL_S("collision2"), FL_S("test123"), FL_S("test456") };
    int num_keys = sizeof(keys) / sizeof(keys[0]);

    for (int i = 0; i < num_keys; i++) {
        hashmap_string_insert(&map, keys[i], i * 10);
    }

    for (int i = 0; i < num_keys; i++) {
        int* val = hashmap_string_get(&map, keys[i]);
        ASSERT_NE(val, NULL);
        ASSERT_EQ(*val, i * 10);
        ASSERT_TRUE(hashmap_string_contains(&map, keys[i]));
    }

    ASSERT_EQ(hashmap_count(&map), (u64)num_keys);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hashmap, string_keys_clear_and_reuse) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    hashmap(FlString, int) map;
    hashmap_new(&map, arena, 8);

    FlString keys[] = { FL_S("key1"), FL_S("key2"), FL_S("key3"), FL_S("key4"), FL_S("key5") };
    for (int i = 0; i < 5; i++) {
        hashmap_string_insert(&map, keys[i], i * 100);
    }

    ASSERT_EQ(hashmap_count(&map), 5UL);

    hashmap_clear(&map);
    ASSERT_EQ(hashmap_count(&map), 0UL);

    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(hashmap_string_get(&map, keys[i]), NULL);
        ASSERT_FALSE(hashmap_string_contains(&map, keys[i]));
    }

    // Should be able to insert again (tests free list)
    FlString new_key = FL_S("newkey");
    hashmap_string_insert(&map, new_key, 9999);
    int* val = hashmap_string_get(&map, new_key);
    ASSERT_NE(val, NULL);
    ASSERT_EQ(*val, 9999);
    ASSERT_EQ(hashmap_count(&map), 1UL);

    fl_arena_destroy(arena);
}
