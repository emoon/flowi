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

typedef fixed_array(i32) IntArray;

typedef struct IntArraySeed {
    u32 capacity;
    const i32* values;
    u32 count;
} IntArraySeed;

static void int_array_init(IntArray* array, FlArena* arena, const IntArraySeed* seed) {
    fixed_array_new(array, arena, seed->capacity);
    for (u32 i = 0; i < seed->count; i++) {
        fixed_array_add(array, seed->values[i]);
    }
}

static void expect_array_state(int* utest_result, IntArray* array, bool empty, bool full) {
    ASSERT_EQ(fixed_array_is_empty(array), empty);
    ASSERT_EQ(fixed_array_is_full(array), full);
}

static void expect_array_values(int* utest_result, IntArray* array, const i32* want, u32 count) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_EQ(fixed_array_get(array, i), want[i]);
    }
}

static void expect_array_ptr_values(int* utest_result, IntArray* array, const i32* want, u32 count) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_EQ(*fixed_array_get_ptr(array, i), want[i]);
    }
}

static void expect_values_equal(int* utest_result, const i32* got, const i32* want, u32 count) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_EQ(got[i], want[i]);
    }
}

UTEST(array, create_and_destroy) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    IntArray int_array;
    fixed_array_new(&int_array, arena, 10);

    ASSERT_EQ(fixed_array_capacity(&int_array), 10U);
    ASSERT_EQ(fixed_array_length(&int_array), 0U);
    ASSERT_NE(int_array.base.data, nullptr);
    expect_array_state(utest_result, &int_array, true, false);

    fl_arena_destroy(arena);
}

UTEST(array, add_and_get) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    fixed_array_new(&int_array, arena, 5);

    const i32 want[3] = { 42, 17, -5 };
    i32 added[3];
    added[0] = fixed_array_add(&int_array, 42);
    added[1] = fixed_array_add(&int_array, 17);
    added[2] = fixed_array_add(&int_array, -5);

    expect_values_equal(utest_result, added, want, 3);
    ASSERT_EQ(fixed_array_length(&int_array), 3U);

    expect_array_values(utest_result, &int_array, want, 3);
    expect_array_ptr_values(utest_result, &int_array, want, 3);

    fl_arena_destroy(arena);
}

UTEST(array, bounds_checking) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    const i32 initial[2] = { 10, 20 };
    int_array_init(&int_array, arena, &(IntArraySeed) { 3, initial, 2 });

    i32 value = fixed_array_get(&int_array, 5);
    ASSERT_EQ(value, 0);

    i32* ptr = fixed_array_get_ptr(&int_array, 5);
    ASSERT_EQ(ptr, nullptr);

    fl_arena_destroy(arena);
}

UTEST(array, set_elements) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    fixed_array_new(&int_array, arena, 5);

    fixed_array_set(&int_array, 0, 100);
    fixed_array_set(&int_array, 2, 200);
    fixed_array_set(&int_array, 1, 150);

    const i32 want[3] = { 100, 150, 200 };
    ASSERT_EQ(fixed_array_length(&int_array), 3U); // the highest index set, plus one
    expect_array_values(utest_result, &int_array, want, 3);

    fl_arena_destroy(arena);
}

UTEST(array, remove_swapback) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    const i32 initial[4] = { 1, 2, 3, 4 };
    int_array_init(&int_array, arena, &(IntArraySeed) { 5, initial, 4 });

    ASSERT_EQ(fixed_array_length(&int_array), 4U);

    i32 removed = fixed_array_remove_swap(&int_array, 1);
    ASSERT_EQ(removed, 2);
    ASSERT_EQ(fixed_array_length(&int_array), 3U);

    const i32 want[3] = { 1, 4, 3 }; // 4 swapped from the end
    expect_array_values(utest_result, &int_array, want, 3);

    fl_arena_destroy(arena);
}

UTEST(array, clear_and_status) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    fixed_array_new(&int_array, arena, 3);

    expect_array_state(utest_result, &int_array, true, false);

    fixed_array_add(&int_array, 1);
    fixed_array_add(&int_array, 2);
    fixed_array_add(&int_array, 3);

    expect_array_state(utest_result, &int_array, false, true);

    fixed_array_clear(&int_array);

    expect_array_state(utest_result, &int_array, true, false);
    ASSERT_EQ(fixed_array_length(&int_array), 0U);
    ASSERT_EQ(fixed_array_capacity(&int_array), 3U);

    fl_arena_destroy(arena);
}

UTEST(array, direct_access) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    const i32 initial[5] = { 0, 10, 20, 30, 40 };
    int_array_init(&int_array, arena, &(IntArraySeed) { 10, initial, 5 });

    ASSERT_EQ(int_array.base.data != nullptr, true);
    expect_values_equal(utest_result, (const i32*)int_array.base.data, initial, 5);

    fl_arena_destroy(arena);
}

UTEST(array, capacity_exceeded) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    fixed_array_new(&int_array, arena, 2);

    i32 val1 = fixed_array_add(&int_array, 1);
    i32 val2 = fixed_array_add(&int_array, 2);

    ASSERT_EQ(val1, 1);
    ASSERT_EQ(val2, 2);
    ASSERT_TRUE(fixed_array_is_full(&int_array));

    i32 val3 = fixed_array_add(&int_array, 3);
    ASSERT_EQ(val3, 0); // a full array returns the zero value
    ASSERT_EQ(fixed_array_length(&int_array), 2U);

    fl_arena_destroy(arena);
}

static void expect_point(int* utest_result, Point got, Point want) {
    ASSERT_EQ(got.x, want.x);
    ASSERT_EQ(got.y, want.y);
}

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

    expect_point(utest_result, retrieved1, p1);
    expect_point(utest_result, retrieved2, p2);

    Point* p3_ptr = fixed_array_get_ptr(&point_array, 0);
    expect_point(utest_result, *p3_ptr, p1);

    fl_arena_destroy(arena);
}

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

UTEST(array, type_safety) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    const i32 initial[1] = { 42 };
    int_array_init(&int_array, arena, &(IntArraySeed) { 5, initial, 1 });

    fixed_array_set(&int_array, 0, 100);

    i32 value = fixed_array_get(&int_array, 0);
    ASSERT_EQ(value, 100);

    // Type mismatches are caught at compile time by static_assert.

    fl_arena_destroy(arena);
}

UTEST(array, for_each_macro) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    IntArray int_array;
    const i32 initial[3] = { 10, 20, 30 };
    int_array_init(&int_array, arena, &(IntArraySeed) { 5, initial, 3 });

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
