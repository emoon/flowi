#include "utest/utest.h"
#include <core/arena.h>
#include <core/pool_allocator.h>

typedef struct TestItem {
    int value;
    float data;
    struct TestItem* next; // Used by free list
} TestItem;

typedef struct SmallItem {
    int id;
    struct SmallItem* next;
} SmallItem;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(pool_allocator, basic_allocation) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    pool(TestItem) pool;

    pool_new(&pool, arena);

    TestItem* item = pool_alloc(&pool);
    ASSERT_NE(item, NULL);
    ASSERT_EQ(item->value, 0);
    ASSERT_EQ(item->data, 0.0f);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(pool_allocator, multiple_allocations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    pool(TestItem) pool;

    pool_new(&pool, arena);

    TestItem* item1 = pool_alloc(&pool);
    TestItem* item2 = pool_alloc(&pool);
    TestItem* item3 = pool_alloc(&pool);

    ASSERT_NE(item1, NULL);
    ASSERT_NE(item2, NULL);
    ASSERT_NE(item3, NULL);
    ASSERT_NE(item1, item2);
    ASSERT_NE(item2, item3);
    ASSERT_NE(item1, item3);

    item1->value = 10;
    item2->value = 20;
    item3->value = 30;

    ASSERT_EQ(item1->value, 10);
    ASSERT_EQ(item2->value, 20);
    ASSERT_EQ(item3->value, 30);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(pool_allocator, free_and_reuse) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    pool(TestItem) pool;

    pool_new(&pool, arena);

    TestItem* item1 = pool_alloc(&pool);
    item1->value = 42;

    pool_free(&pool, item1);

    TestItem* item2 = pool_alloc(&pool);
    ASSERT_EQ(item1, item2);
    ASSERT_EQ(item2->value, 0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(pool_allocator, free_list_lifo_behavior) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    pool(TestItem) pool;

    pool_new(&pool, arena);

    TestItem* item1 = pool_alloc(&pool);
    TestItem* item2 = pool_alloc(&pool);
    TestItem* item3 = pool_alloc(&pool);

    pool_free(&pool, item1);
    pool_free(&pool, item2);
    pool_free(&pool, item3);

    TestItem* reuse1 = pool_alloc(&pool);
    TestItem* reuse2 = pool_alloc(&pool);
    TestItem* reuse3 = pool_alloc(&pool);

    ASSERT_EQ(reuse1, item3);
    ASSERT_EQ(reuse2, item2);
    ASSERT_EQ(reuse3, item1);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(pool_allocator, mixed_allocation_and_free) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    pool(SmallItem) pool;

    pool_new(&pool, arena);

    SmallItem* item1 = pool_alloc(&pool);
    SmallItem* item2 = pool_alloc(&pool);

    item1->id = 100;
    item2->id = 200;

    pool_free(&pool, item1);

    SmallItem* item3 = pool_alloc(&pool);
    ASSERT_EQ(item3, item1);
    ASSERT_EQ(item3->id, 0);

    item3->id = 300;

    SmallItem* item4 = pool_alloc(&pool);
    ASSERT_NE(item4, item1);
    ASSERT_NE(item4, item2);
    ASSERT_NE(item4, item3);

    ASSERT_EQ(item2->id, 200);
    ASSERT_EQ(item3->id, 300);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(pool_allocator, different_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    pool(TestItem) test_pool;
    pool(SmallItem) small_pool;

    pool_new(&test_pool, arena);
    pool_new(&small_pool, arena);

    TestItem* test_item = pool_alloc(&test_pool);
    SmallItem* small_item = pool_alloc(&small_pool);

    ASSERT_NE(test_item, NULL);
    ASSERT_NE(small_item, NULL);

    test_item->value = 500;
    test_item->data = 3.14f;
    small_item->id = 999;

    ASSERT_EQ(test_item->value, 500);
    ASSERT_EQ(test_item->data, 3.14f);
    ASSERT_EQ(small_item->id, 999);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Test definitions only - main runner in core_tests.c
