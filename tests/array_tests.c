#include "utest/utest.h"
#include <core/arena.h>
#include <core/fixed_array.h>
#include <stddef.h>

typedef struct {
    i32 x;
    i32 y;
} Point;

typedef struct {
    i32 id;
    f32 value;
} FloatItem;

typedef struct {
    i16 id;
    f32 score;
} TestStruct;

typedef struct {
    void* ptr;
    i32 value;
} PtrValueTestStruct;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, create_and_destroy) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 10);

    ASSERT_EQ(fixed_array_capacity(&int_array), 10U);
    ASSERT_EQ(fixed_array_length(&int_array), 0U);
    ASSERT_NE(int_array.base.data, nullptr);
    ASSERT_TRUE(fixed_array_is_empty(&int_array));
    ASSERT_FALSE(fixed_array_is_full(&int_array));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, add_and_get) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 5);

    i32 val1 = fixed_array_add(&int_array, 42);
    i32 val2 = fixed_array_add(&int_array, 17);
    i32 val3 = fixed_array_add(&int_array, -5);

    ASSERT_EQ(val1, 42);
    ASSERT_EQ(val2, 17);
    ASSERT_EQ(val3, -5);
    ASSERT_EQ(fixed_array_length(&int_array), 3U);

    ASSERT_EQ(fixed_array_get(&int_array, 0), 42);
    ASSERT_EQ(fixed_array_get(&int_array, 1), 17);
    ASSERT_EQ(fixed_array_get(&int_array, 2), -5);

    ASSERT_EQ(*fixed_array_get_ptr(&int_array, 0), 42);
    ASSERT_EQ(*fixed_array_get_ptr(&int_array, 1), 17);
    ASSERT_EQ(*fixed_array_get_ptr(&int_array, 2), -5);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, bounds_checking) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 3);

    fixed_array_add(&int_array, 10);
    fixed_array_add(&int_array, 20);

    i32 value = fixed_array_get(&int_array, 5);
    ASSERT_EQ(value, 0);

    i32* ptr = fixed_array_get_ptr(&int_array, 5);
    ASSERT_EQ(ptr, nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, set_elements) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 5);

    fixed_array_set(&int_array, 0, 100);
    fixed_array_set(&int_array, 2, 200);
    fixed_array_set(&int_array, 1, 150);

    ASSERT_EQ(fixed_array_length(&int_array), 3U); // Should grow to accommodate highest index + 1
    ASSERT_EQ(fixed_array_get(&int_array, 0), 100);
    ASSERT_EQ(fixed_array_get(&int_array, 1), 150);
    ASSERT_EQ(fixed_array_get(&int_array, 2), 200);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, remove_swapback) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 5);

    fixed_array_add(&int_array, 1);
    fixed_array_add(&int_array, 2);
    fixed_array_add(&int_array, 3);
    fixed_array_add(&int_array, 4);

    ASSERT_EQ(fixed_array_length(&int_array), 4U);

    i32 removed = fixed_array_remove_swap(&int_array, 1);
    ASSERT_EQ(removed, 2);
    ASSERT_EQ(fixed_array_length(&int_array), 3U);

    ASSERT_EQ(fixed_array_get(&int_array, 0), 1);
    ASSERT_EQ(fixed_array_get(&int_array, 1), 4); // Swapped from end
    ASSERT_EQ(fixed_array_get(&int_array, 2), 3);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, clear_and_status) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 3);

    ASSERT_TRUE(fixed_array_is_empty(&int_array));
    ASSERT_FALSE(fixed_array_is_full(&int_array));

    fixed_array_add(&int_array, 1);
    fixed_array_add(&int_array, 2);
    fixed_array_add(&int_array, 3);

    ASSERT_FALSE(fixed_array_is_empty(&int_array));
    ASSERT_TRUE(fixed_array_is_full(&int_array));

    fixed_array_clear(&int_array);

    ASSERT_TRUE(fixed_array_is_empty(&int_array));
    ASSERT_FALSE(fixed_array_is_full(&int_array));
    ASSERT_EQ(fixed_array_length(&int_array), 0U);
    ASSERT_EQ(fixed_array_capacity(&int_array), 3U);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, direct_access) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 10);

    for (i32 i = 0; i < 5; i++) {
        fixed_array_add(&int_array, i * 10);
    }

    ASSERT_EQ(int_array.base.data != nullptr, true);
    i32* raw_data = (i32*)int_array.base.data;
    ASSERT_EQ(raw_data[0], 0);
    ASSERT_EQ(raw_data[1], 10);
    ASSERT_EQ(raw_data[2], 20);
    ASSERT_EQ(raw_data[3], 30);
    ASSERT_EQ(raw_data[4], 40);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, capacity_exceeded) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 2);

    i32 val1 = fixed_array_add(&int_array, 1);
    i32 val2 = fixed_array_add(&int_array, 2);

    ASSERT_EQ(val1, 1);
    ASSERT_EQ(val2, 2);
    ASSERT_TRUE(fixed_array_is_full(&int_array));

    i32 val3 = fixed_array_add(&int_array, 3);
    ASSERT_EQ(val3, 0); // Should return default value when full
    ASSERT_EQ(fixed_array_length(&int_array), 2U);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, struct_array) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(Point) point_array;
    fixed_array_new(&point_array, arena, 5);

    Point p1 = { 10, 20 };
    Point p2 = { 30, 40 };

    fixed_array_add(&point_array, p1);
    fixed_array_add(&point_array, p2);

    Point retrieved1 = fixed_array_get(&point_array, 0);
    Point retrieved2 = fixed_array_get(&point_array, 1);

    ASSERT_EQ(retrieved1.x, 10);
    ASSERT_EQ(retrieved1.y, 20);
    ASSERT_EQ(retrieved2.x, 30);
    ASSERT_EQ(retrieved2.y, 40);

    Point* p3_ptr = fixed_array_get_ptr(&point_array, 0);
    ASSERT_EQ(p3_ptr->x, 10);
    ASSERT_EQ(p3_ptr->y, 20);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, float_array) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(f32) float_array;
    fixed_array_new(&float_array, arena, 3);

    fixed_array_add(&float_array, 3.14f);
    fixed_array_add(&float_array, 2.71f);

    ASSERT_NEAR(fixed_array_get(&float_array, 0), 3.14f, 0.001f);
    ASSERT_NEAR(fixed_array_get(&float_array, 1), 2.71f, 0.001f);

    f32* fp0 = fixed_array_get_ptr(&float_array, 0);
    f32* fp1 = fixed_array_get_ptr(&float_array, 1);
    ANALYZER_ASSUME_NONNULL(fp0);
    ANALYZER_ASSUME_NONNULL(fp1);
    ASSERT_NEAR(*fp0, 3.14f, 0.001f);
    ASSERT_NEAR(*fp1, 2.71f, 0.001f);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, type_safety) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 5);

    fixed_array_add(&int_array, 42);
    fixed_array_set(&int_array, 0, 100);

    i32 value = fixed_array_get(&int_array, 0);
    ASSERT_EQ(value, 100);

    // Type mismatches are caught at compile time by static_assert.

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(array, for_each_macro) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    fixed_array(i32) int_array;
    fixed_array_new(&int_array, arena, 5);

    fixed_array_add(&int_array, 10);
    fixed_array_add(&int_array, 20);
    fixed_array_add(&int_array, 30);

    i32 sum = 0;
    i32 count = 0;
    for_each_fixed_array(value, &int_array) {
        sum += value;
        count++;
    }

    ASSERT_EQ(sum, 60);
    ASSERT_EQ(count, 3);

    fl_arena_destroy(arena);
}
