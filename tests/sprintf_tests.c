#include "utest/utest.h"
#include <core/arena.h>
#include <core/simd.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <limits.h>
#include <string.h>

UTEST(sprintf, basic_format) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "Hello %s, number: %d", "World", 42);

    const char* expected = "Hello World, number: 42";
    ASSERT_EQ((u64)result.length, strlen(expected));
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, string_struct_single) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString test_string = FL_S("TestString");
    FlString result = fl_sprintf_arena(arena, "FlString: %S", test_string);

    const char* expected = "FlString: TestString";
    ASSERT_EQ((u64)result.length, strlen(expected));
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, string_struct_multiple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString str1 = FL_S("First");
    FlString str2 = FL_S("Second");
    FlString result = fl_sprintf_arena(arena, "%S and %S", str1, str2);

    const char* expected = "First and Second";
    ASSERT_EQ((u64)result.length, strlen(expected));
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, mixed_format_specifiers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString name = FL_S("Alice");
    FlString result = fl_sprintf_arena(arena, "Name: %S, Age: %d", name, 25);

    const char* expected = "Name: Alice, Age: 25";
    ASSERT_EQ((u64)result.length, strlen(expected));
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, empty_string_struct) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString empty_string = { .data = "", .length = 0 };
    FlString result = fl_sprintf_arena(arena, "Empty: '%S'", empty_string);

    const char* expected = "Empty: ''";
    ASSERT_EQ((u64)result.length, strlen(expected));
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vsprintf_arena) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    // Test that vsprintf_arena works through fl_sprintf_arena
    FlString test = FL_S("test");
    FlString result = fl_sprintf_arena(arena, "Testing %S with %s", test, "cstring");

    const char* expected = "Testing test with cstring";
    ASSERT_EQ((u64)result.length, strlen(expected));
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_f32x4_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    f32x4 vec = f32x4_new(1.0f, 2.5f, -3.75f, 0.0f);
    FlString result = fl_sprintf_arena(arena, "Vector: %v4f", vec);

    const char* expected = "Vector: [1, 2.5, -3.75, 0]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_i32x4_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i32x4 vec = i32x4_new(42, -17, 1024, 0);
    FlString result = fl_sprintf_arena(arena, "IntVec: %v4i", vec);

    const char* expected = "IntVec: [42, -17, 1024, 0]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_u32x4_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    u32x4 vec = u32x4_new(255, 128, 64, 4294967295U);
    FlString result = fl_sprintf_arena(arena, "UIntVec: %v4u", vec);

    const char* expected = "UIntVec: [255, 128, 64, 4294967295]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_f32x4_mixed_format) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    f32x4 pos = f32x4_new(10.5f, 20.25f, -5.0f, 1.0f);
    f32x4 vel = f32x4_new(1.0f, 0.0f, -2.5f, 0.0f);
    FlString result = fl_sprintf_arena(arena, "pos: %v4f, vel: %v4f", pos, vel);

    const char* expected = "pos: [10.5, 20.25, -5, 1], vel: [1, 0, -2.5, 0]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_all_types_mixed) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    f32x4 fvec = f32x4_new(1.5f, 2.5f, 3.5f, 4.5f);
    i32x4 ivec = i32x4_new(10, 20, 30, 40);
    u32x4 uvec = u32x4_new(100, 200, 300, 400);

    FlString result = fl_sprintf_arena(arena, "f: %v4f, i: %v4i, u: %v4u", fvec, ivec, uvec);

    const char* expected = "f: [1.5, 2.5, 3.5, 4.5], i: [10, 20, 30, 40], u: [100, 200, 300, 400]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_edge_cases) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    f32x4 zero = f32x4_new(0.0f, 0.0f, 0.0f, 0.0f);
    FlString result1 = fl_sprintf_arena(arena, "%v4f", zero);
    const char* expected1 = "[0, 0, 0, 0]";
    ASSERT_STREQ(result1.data, expected1);

    i32x4 neg = i32x4_new(-1, -2, -3, -4);
    FlString result2 = fl_sprintf_arena(arena, "%v4i", neg);
    const char* expected2 = "[-1, -2, -3, -4]";
    ASSERT_STREQ(result2.data, expected2);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_i16x8_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i16x8 vec = i16x8_new(1, 2, 3, 4, -5, -6, -7, -8);
    FlString result = fl_sprintf_arena(arena, "i16x8: %v8i16", vec);

    const char* expected = "i16x8: [1, 2, 3, 4, -5, -6, -7, -8]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_u8x16_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const uint8_t data[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 vec = u8x16_new(data);
    FlString result = fl_sprintf_arena(arena, "u8x16: %v16u8", vec);

    const char* expected = "u8x16: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_u16x8_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i16x8 temp = i16x8_new(100, 200, 300, 400, 500, 600, 700, 800);
    u16x8 vec = (u16x8) { .v = temp.v };
    FlString result = fl_sprintf_arena(arena, "u16x8: %v8u16", vec);

    const char* expected = "u16x8: [100, 200, 300, 400, 500, 600, 700, 800]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_f16x8_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    f16x8 vec = f16x8_new(1.0f, 2.5f, -3.25f, 0.0f, 0.5f, -1.5f, 2.0f, -4.0f);
    FlString result = fl_sprintf_arena(arena, "f16x8: %v8h", vec);

    const char* expected = "f16x8: [1, 2.5, -3.25, 0, 0.5, -1.5, 2, -4]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_extended_types_mixed) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i16x8 i16vec = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);
    const uint8_t u8data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    u8x16 u8vec = u8x16_new(u8data);

    FlString result = fl_sprintf_arena(arena, "i16: %v8i16, u8: %v16u8", i16vec, u8vec);

    const char* expected
        = "i16: [10, 20, 30, 40, 50, 60, 70, 80], u8: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf, vector_unknown_format) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    // Test unknown vector format - should be treated as literal text
    FlString result = fl_sprintf_arena(arena, "Unknown: %v8x", 42);

    const char* expected = "Unknown: v8x";
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, integer_formats) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%d %i %u", 42, -17, 3000000000U);
    ASSERT_STREQ(result.data, "42 -17 3000000000");

    result = fl_sprintf_arena(arena, "%o %x %X", 64, 255, 255);
    ASSERT_STREQ(result.data, "100 ff FF");

    result = fl_sprintf_arena(arena, "%#o %#x %#X", 64, 255, 255);
    ASSERT_STREQ(result.data, "0100 0xff 0XFF");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, width_precision) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%5d %5d", 42, -17);
    ASSERT_STREQ(result.data, "   42   -17");

    result = fl_sprintf_arena(arena, "%05d %05d", 42, -17);
    ASSERT_STREQ(result.data, "00042 -0017");

    result = fl_sprintf_arena(arena, "%.5d %.5d", 42, -17);
    ASSERT_STREQ(result.data, "00042 -00017");

    result = fl_sprintf_arena(arena, "%10.5d", 42);
    ASSERT_STREQ(result.data, "     00042");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, left_justify) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%-5d|%-5d|", 42, -17);
    ASSERT_STREQ(result.data, "42   |-17  |");

    // Left justify with zero flag (zero ignored)
    result = fl_sprintf_arena(arena, "%-05d", 42);
    ASSERT_STREQ(result.data, "42   ");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, sign_flags) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%+d %+d", 42, -17);
    ASSERT_STREQ(result.data, "+42 -17");

    // Space flag - shows space for positive numbers
    result = fl_sprintf_arena(arena, "% d % d", 42, -17);
    ASSERT_STREQ(result.data, " 42 -17");

    // Space flag takes precedence when combined with plus flag
    result = fl_sprintf_arena(arena, "%+ d", 42);
    ASSERT_STREQ(result.data, " 42");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, float_formats) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%f %F", 3.14159, 3.14159);
    ASSERT_TRUE(strncmp(result.data, "3.14159", 7) == 0);

    result = fl_sprintf_arena(arena, "%e %E", 1234.5, 1234.5);
    ASSERT_TRUE(strstr(result.data, "1.2345") != NULL);
    ASSERT_TRUE(strstr(result.data, "e+03") != NULL || strstr(result.data, "E+03") != NULL);

    result = fl_sprintf_arena(arena, "%g %G", 0.00012345, 123450000.0);
    ASSERT_TRUE(strstr(result.data, "0.00012345") != NULL);

    result = fl_sprintf_arena(arena, "%.2f %.4f", 3.14159, 3.14159);
    ASSERT_STREQ(result.data, "3.14 3.1416");

    result = fl_sprintf_arena(arena, "%f", 0.0);
    ASSERT_STREQ(result.data, "0.000000");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, string_formats) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%s", "hello");
    ASSERT_STREQ(result.data, "hello");

    result = fl_sprintf_arena(arena, "%10s", "hello");
    ASSERT_STREQ(result.data, "     hello");

    result = fl_sprintf_arena(arena, "%-10s|", "hello");
    ASSERT_STREQ(result.data, "hello     |");

    result = fl_sprintf_arena(arena, "%.3s", "hello");
    ASSERT_STREQ(result.data, "hel");

    result = fl_sprintf_arena(arena, "%10.3s", "hello");
    ASSERT_STREQ(result.data, "       hel");

    result = fl_sprintf_arena(arena, "%s", (char*)NULL);
    ASSERT_STREQ(result.data, "null");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, char_format) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%c %c", 'A', 65);
    ASSERT_STREQ(result.data, "A A");

    result = fl_sprintf_arena(arena, "%5c", 'X');
    ASSERT_STREQ(result.data, "    X");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, pointer_format) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    void* ptr = (void*)0x12345678;
    void* null_ptr = NULL;

    FlString result = fl_sprintf_arena(arena, "%p", ptr);
    ASSERT_TRUE(strstr(result.data, "12345678") != NULL || strstr(result.data, "0x12345678") != NULL);

    result = fl_sprintf_arena(arena, "%p", null_ptr);
    ASSERT_TRUE(strstr(result.data, "0") != NULL || strstr(result.data, "(nil)") != NULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_std, percent_literal) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "100%% complete");
    ASSERT_STREQ(result.data, "100% complete");

    result = fl_sprintf_arena(arena, "%%d gives %d", 42);
    ASSERT_STREQ(result.data, "%d gives 42");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_edge, extreme_values) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%d %u", INT_MAX, UINT_MAX);
    char expected[64];
    snprintf(expected, sizeof(expected), "%d %u", INT_MAX, UINT_MAX);
    ASSERT_STREQ(result.data, expected);

    result = fl_sprintf_arena(arena, "%d %d", INT_MIN, 0);
    snprintf(expected, sizeof(expected), "%d %d", INT_MIN, 0);
    ASSERT_STREQ(result.data, expected);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_edge, width_from_arg) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%*d", 5, 42);
    ASSERT_STREQ(result.data, "   42");

    result = fl_sprintf_arena(arena, "%*d", -5, 42);
    ASSERT_STREQ(result.data, "42");

    result = fl_sprintf_arena(arena, "%.*f", 2, 3.14159);
    ASSERT_STREQ(result.data, "3.14");

    result = fl_sprintf_arena(arena, "%*.*f", 10, 2, 3.14159);
    ASSERT_STREQ(result.data, "      3.14");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_edge, zero_width_precision) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = fl_sprintf_arena(arena, "%.0s", "hello");
    ASSERT_EQ((u64)result.length, 0UL);
    ASSERT_STREQ(result.data, "");

    // Zero precision for integer - stb_sprintf prints 0 for zero value
    result = fl_sprintf_arena(arena, "%.0d", 0);
    ASSERT_STREQ(result.data, "0");

    result = fl_sprintf_arena(arena, "%.0d", 42);
    ASSERT_STREQ(result.data, "42");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(sprintf_combined, string_with_all_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString test_str = FL_S("TestString");

    FlString result = fl_sprintf_arena(arena, "str:%S int:%d float:%.1f char:%c", test_str, 42, 3.14, 'X');

    ASSERT_TRUE(strstr(result.data, "TestString") != NULL);
    ASSERT_TRUE(strstr(result.data, "42") != NULL);
    ASSERT_TRUE(strstr(result.data, "3.1") != NULL);
    ASSERT_TRUE(strstr(result.data, "X") != NULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A uniform per-thread fill character formatted over more than one stb chunk makes a byte spliced in
// by another thread's allocation on the shared arena detectable at any offset in the result.

#define SPRINTF_SHARED_THREADS 4
#define SPRINTF_SHARED_ITERS 128

typedef struct SprintfSharedWorker {
    FlArena* arena;
    int id;
    bool ok;
} SprintfSharedWorker;

static void* sprintf_shared_worker(void* user) {
    SprintfSharedWorker* w = (SprintfSharedWorker*)user;
    const char fill = (char)('A' + w->id);

    for (int i = 0; i < SPRINTF_SHARED_ITERS; ++i) {
        const int len = 600 + (i % 300); // always > 512 -> spans more than one stb chunk
        char expected[1024];
        memset(expected, fill, (size_t)len);
        expected[len] = 0;

        FlString s = fl_sprintf_arena(w->arena, "%s", expected);
        if ((int)s.length != len || memcmp(s.data, expected, (size_t)len) != 0) {
            w->ok = false;
            return nullptr;
        }
    }

    w->ok = true;
    return nullptr;
}

UTEST(sprintf, shared_arena_concurrent_contiguous) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    SprintfSharedWorker workers[SPRINTF_SHARED_THREADS];
    Thread threads[SPRINTF_SHARED_THREADS];
    for (int t = 0; t < SPRINTF_SHARED_THREADS; ++t) {
        workers[t] = (SprintfSharedWorker) { .arena = arena, .id = t, .ok = false };
        ASSERT_TRUE(os_thread_create(&threads[t], sprintf_shared_worker, &workers[t]));
    }

    for (int t = 0; t < SPRINTF_SHARED_THREADS; ++t) {
        os_thread_join(&threads[t]);
        ASSERT_TRUE_MSG(workers[t].ok, "a worker saw a corrupt/non-contiguous fl_sprintf_arena result");
    }

    fl_arena_destroy(arena);
}
