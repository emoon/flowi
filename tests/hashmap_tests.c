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

typedef hashmap(int, int) IntMap;
typedef hashmap(FlString, int) StringIntMap;

static void expect_int_value(int* utest_result, const int* val, int want) {
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(*val, want);
}

static void expect_int_contains(int* utest_result, IntMap* map, int key, bool want) {
    ASSERT_EQ(hashmap_contains(map, key), want);
}

static void expect_string_value(int* utest_result, StringIntMap* map, FlString key, int want) {
    expect_int_value(utest_result, hashmap_string_get(map, key), want);
}

static void expect_string_contains(int* utest_result, StringIntMap* map, FlString key, bool want) {
    ASSERT_EQ(hashmap_string_contains(map, key), want);
}

UTEST(hashmap, int_to_int) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    IntMap map;
    hashmap_new(&map, arena, 16);

    hashmap_insert(&map, 42, 100);
    hashmap_insert(&map, 7, 200);
    hashmap_insert(&map, 99, 300);

    expect_int_value(utest_result, hashmap_get(&map, 42), 100);
    expect_int_value(utest_result, hashmap_get(&map, 7), 200);
    expect_int_value(utest_result, hashmap_get(&map, 99), 300);

    int* val = hashmap_get(&map, 123);
    ASSERT_EQ(val, nullptr);

    hashmap_insert(&map, 42, 999);
    expect_int_value(utest_result, hashmap_get(&map, 42), 999);

    ASSERT_EQ(hashmap_count(&map), 3UL);

    expect_int_contains(utest_result, &map, 42, true);
    expect_int_contains(utest_result, &map, 7, true);
    expect_int_contains(utest_result, &map, 99, true);
    expect_int_contains(utest_result, &map, 123, false);

    ASSERT_TRUE(hashmap_remove(&map, 7));
    ASSERT_EQ(hashmap_count(&map), 2UL);
    expect_int_contains(utest_result, &map, 7, false);
    ASSERT_FALSE(hashmap_remove(&map, 7));

    fl_arena_destroy(arena);
}

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
    ASSERT_NE(val, nullptr);
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
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(*val, 222);

    int obj4 = 0;
    void* ptr4 = &obj4;
    val = hashmap_get(&map, ptr4);
    ASSERT_EQ(val, nullptr);

    fl_arena_destroy(arena);
}

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
        ASSERT_EQ(hashmap_get(&map, i), nullptr);
    }

    // An insert after a clear takes its node from the free list.
    hashmap_insert(&map, (u64)42, (u64)4200);
    u64* val = hashmap_get(&map, (u64)42);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(*val, 4200UL);
    ASSERT_EQ(hashmap_count(&map), 1UL);

    fl_arena_destroy(arena);
}

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
        ASSERT_NE(val, nullptr);
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
            ASSERT_EQ(val, nullptr);
        } else {
            ASSERT_NE(val, nullptr);
            ASSERT_EQ(*val, i * 100);
        }
    }

    fl_arena_destroy(arena);
}

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
        ASSERT_NE(val, nullptr);
        ASSERT_EQ((int)val->data[0], 'A' + i);
    }

    ASSERT_EQ(hashmap_count(&map), 20UL);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
    hashmap_new(&map, arena, 16);

    FlString key1 = FL_S("hello");
    FlString key2 = FL_S("world");
    FlString key3 = FL_S("test");

    hashmap_string_insert(&map, key1, 100);
    hashmap_string_insert(&map, key2, 200);
    hashmap_string_insert(&map, key3, 300);

    expect_string_value(utest_result, &map, key1, 100);
    expect_string_value(utest_result, &map, key2, 200);
    expect_string_value(utest_result, &map, key3, 300);

    ASSERT_EQ(hashmap_count(&map), 3UL);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_same_content_different_memory) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
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

    expect_string_value(utest_result, &map, key2, 42);

    expect_string_contains(utest_result, &map, key1, true);
    expect_string_contains(utest_result, &map, key2, true);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_empty_strings) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
    hashmap_new(&map, arena, 16);

    FlString empty1 = FL_S("");
    FlString empty2 = fl_string_copy(arena, FL_S(""));

    hashmap_string_insert(&map, empty1, 999);

    expect_string_value(utest_result, &map, empty2, 999);

    expect_string_contains(utest_result, &map, empty1, true);
    expect_string_contains(utest_result, &map, empty2, true);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_update_value) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
    hashmap_new(&map, arena, 16);

    FlString key = FL_S("updateme");

    hashmap_string_insert(&map, key, 100);
    expect_string_value(utest_result, &map, key, 100);

    hashmap_string_insert(&map, key, 200);
    expect_string_value(utest_result, &map, key, 200);

    ASSERT_EQ(hashmap_count(&map), 1UL);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_remove) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
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
    expect_string_contains(utest_result, &map, key1, false);
    expect_string_contains(utest_result, &map, key2, true);
    expect_string_contains(utest_result, &map, key3, true);

    ASSERT_FALSE(hashmap_string_remove(&map, key1));

    FlString key2_copy = fl_string_copy(arena, FL_S("remove2"));
    ASSERT_TRUE(hashmap_string_remove(&map, key2_copy));
    ASSERT_EQ(hashmap_count(&map), 1UL);
    expect_string_contains(utest_result, &map, key2, false);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_iteration) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
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

UTEST(hashmap, string_keys_collision_handling) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    // A small capacity makes collisions likely.
    StringIntMap map;
    hashmap_new(&map, arena, 4);

    FlString keys[] = { FL_S("a"),   FL_S("aa"),         FL_S("aaa"),        FL_S("b"),       FL_S("bb"),
                        FL_S("bbb"), FL_S("collision1"), FL_S("collision2"), FL_S("test123"), FL_S("test456") };
    int num_keys = sizeof_array(keys);

    for (int i = 0; i < num_keys; i++) {
        hashmap_string_insert(&map, keys[i], i * 10);
    }

    for (int i = 0; i < num_keys; i++) {
        expect_string_value(utest_result, &map, keys[i], i * 10);
        expect_string_contains(utest_result, &map, keys[i], true);
    }

    ASSERT_EQ(hashmap_count(&map), (u64)num_keys);

    fl_arena_destroy(arena);
}

UTEST(hashmap, string_keys_clear_and_reuse) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    StringIntMap map;
    hashmap_new(&map, arena, 8);

    FlString keys[] = { FL_S("key1"), FL_S("key2"), FL_S("key3"), FL_S("key4"), FL_S("key5") };
    for (int i = 0; i < 5; i++) {
        hashmap_string_insert(&map, keys[i], i * 100);
    }

    ASSERT_EQ(hashmap_count(&map), 5UL);

    hashmap_clear(&map);
    ASSERT_EQ(hashmap_count(&map), 0UL);

    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(hashmap_string_get(&map, keys[i]), nullptr);
        expect_string_contains(utest_result, &map, keys[i], false);
    }

    // An insert after a clear takes its node from the free list.
    FlString new_key = FL_S("newkey");
    hashmap_string_insert(&map, new_key, 9999);
    expect_string_value(utest_result, &map, new_key, 9999);
    ASSERT_EQ(hashmap_count(&map), 1UL);

    fl_arena_destroy(arena);
}
