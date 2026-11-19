#include "simd_test_support.h"

UTEST(f32x4, load_store_unaligned) {
    const float data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    f32x4 vec = f32x4_load_unaligned(data);

    float result[4];
    f32x4_store_unaligned(vec, result);

    ASSERT_TRUE(f32_arrays_equal(data, result, 4, 1e-6f));
}

UTEST(f32x4, new_splat) {
    expect_f32x4(utest_result, f32x4_new_splat(5.0f), (const float[4]) { 5.0f, 5.0f, 5.0f, 5.0f });
}

UTEST(f32x4, new_xy) {
    expect_f32x4(utest_result, f32x4_new_xy(1.0f, 2.0f), (const float[4]) { 1.0f, 2.0f, 1.0f, 2.0f });
}

UTEST(f32x4, new) {
    expect_f32x4(utest_result, f32x4_new(1.0f, 2.0f, 3.0f, 4.0f), (const float[4]) { 1.0f, 2.0f, 3.0f, 4.0f });
}

UTEST(f32x4, add) {
    f32x4 a = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 b = f32x4_new(4.0f, 3.0f, 2.0f, 1.0f);

    expect_f32x4(utest_result, f32x4_add(a, b), (const float[4]) { 5.0f, 5.0f, 5.0f, 5.0f });
}

UTEST(f32x4, sub) {
    f32x4 a = f32x4_new(5.0f, 7.0f, 9.0f, 11.0f);
    f32x4 b = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);

    expect_f32x4(utest_result, f32x4_sub(a, b), (const float[4]) { 4.0f, 5.0f, 6.0f, 7.0f });
}

UTEST(f32x4, mul) {
    f32x4 a = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 b = f32x4_new(2.0f, 3.0f, 4.0f, 5.0f);

    expect_f32x4(utest_result, f32x4_mul(a, b), (const float[4]) { 2.0f, 6.0f, 12.0f, 20.0f });
}

UTEST(f32x4, drop_fraction) {
    f32x4 a = f32x4_new(1.9f, 2.1f, 3.7f, 4.3f);

    expect_f32x4(utest_result, f32x4_drop_fraction(a), (const float[4]) { 1.0f, 2.0f, 3.0f, 4.0f });
}

UTEST(f32x4, floor) {
    f32x4 a = f32x4_new(1.9f, 2.1f, 3.7f, 4.3f);

    expect_f32x4(utest_result, f32x4_floor(a), (const float[4]) { 1.0f, 2.0f, 3.0f, 4.0f });
}

UTEST(f32x4, sqrt) {
    f32x4 a = f32x4_new(1.0f, 4.0f, 9.0f, 16.0f);

    expect_f32x4(utest_result, f32x4_sqrt(a), (const float[4]) { 1.0f, 2.0f, 3.0f, 4.0f });
}

UTEST(f32x4, shuffle_functions) {
    f32x4 vec = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);

    expect_f32x4(utest_result, f32x4_shuffle(vec, vec, 0, 1, 0, 1), (const float[4]) { 1.0f, 2.0f, 1.0f, 2.0f });
    expect_f32x4(utest_result, f32x4_shuffle(vec, vec, 2, 3, 2, 3), (const float[4]) { 3.0f, 4.0f, 3.0f, 4.0f });

    // Indices 4 and up select from the second vector.
    f32x4 vec2 = f32x4_new(5.0f, 6.0f, 7.0f, 8.0f);
    expect_f32x4(utest_result, f32x4_shuffle(vec, vec2, 0, 1, 4, 5), (const float[4]) { 1.0f, 2.0f, 5.0f, 6.0f });
}

UTEST(f32x4, and_function) {
    f32x4 a = f32x4_new(1.0f, -1.0f, 2.0f, -2.0f);
    f32x4 mask = f32x4_new(1.0f, 1.0f, 0.0f, 0.0f);

    // Bitwise AND of the IEEE-754 patterns: 1.0f (0x3F800000) survives against itself and against -1.0f
    // (0xBF800000, sign bit cleared); anything ANDed with 0.0f (all-zero) is 0.0f.
    expect_f32x4(utest_result, f32x4_and(a, mask), (const float[4]) { 1.0f, 1.0f, 0.0f, 0.0f });
}

UTEST(f32x4, test_intersect) {
    f32x4 a = f32x4_new(1.0f, 1.0f, 3.0f, 3.0f);
    f32x4 b = f32x4_new(2.0f, 2.0f, 4.0f, 4.0f);
    ASSERT_TRUE(f32x4_test_intersect(a, b));

    f32x4 c = f32x4_new(1.0f, 1.0f, 2.0f, 2.0f);
    f32x4 d = f32x4_new(3.0f, 3.0f, 4.0f, 4.0f);
    ASSERT_FALSE(f32x4_test_intersect(c, d));
}

UTEST(f32x4, extract_all_lanes) {
    f32x4 vec = f32x4_new(1.1f, 2.2f, 3.3f, 4.4f);

    expect_f32x4(utest_result, vec, (const float[4]) { 1.1f, 2.2f, 3.3f, 4.4f });
}

UTEST(f32x4, table_shuffle) {
    f32x4 vec = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);

    // Identity shuffle table (each f32 is 4 bytes)
    const uint8_t table_identity[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

    expect_f32x4(utest_result, f32x4_table_shuffle(vec, table_identity), (const float[4]) { 1.0f, 2.0f, 3.0f, 4.0f });
}

UTEST(i32x4, new_splat) {
    expect_i32x4(utest_result, i32x4_new_splat(42), (const int32_t[4]) { 42, 42, 42, 42 });
}

UTEST(i32x4, new) {
    expect_i32x4(utest_result, i32x4_new(1, 2, 3, 4), (const int32_t[4]) { 1, 2, 3, 4 });
}

UTEST(i32x4, add) {
    i32x4 a = i32x4_new(1, 2, 3, 4);
    i32x4 b = i32x4_new(4, 3, 2, 1);

    expect_i32x4(utest_result, i32x4_add(a, b), (const int32_t[4]) { 5, 5, 5, 5 });
}

UTEST(i32x4, min) {
    i32x4 a = i32x4_new(1, 4, 3, 8);
    i32x4 b = i32x4_new(2, 3, 4, 7);

    expect_i32x4(utest_result, i32x4_min(a, b), (const int32_t[4]) { 1, 3, 3, 7 });
}

UTEST(i32x4, max) {
    i32x4 a = i32x4_new(1, 4, 3, 8);
    i32x4 b = i32x4_new(2, 3, 4, 7);

    expect_i32x4(utest_result, i32x4_max(a, b), (const int32_t[4]) { 2, 4, 4, 8 });
}

UTEST(i32x4, extract) {
    i32x4 vec = i32x4_new(10, 20, 30, 40);

    expect_i32x4(utest_result, vec, (const int32_t[4]) { 10, 20, 30, 40 });
}

UTEST(i32x4, div) {
    i32x4 a = i32x4_new(100, 50, 24, 16);
    i32x4 b = i32x4_new(10, 5, 6, 4);

    expect_i32x4(utest_result, i32x4_div(a, b), (const int32_t[4]) { 10, 10, 4, 4 });
}

UTEST(i32x4, div_gradient_case) {
    i32x4 numerator = i32x4_new_splat(0x8000);
    i32x4 denominator = i32x4_new(1919, 1079, 999, 499); // width-1, height-1 for common resolutions

    // 32768 / (width-1), as in fixed-point step calculations.
    expect_i32x4(utest_result, i32x4_div(numerator, denominator), (const int32_t[4]) { 17, 30, 33, 66 });
}

UTEST(i32x4, shift_left_comprehensive) {
    i32x4 vec = i32x4_new(1, 2, 4, 8);

    expect_i32x4(utest_result, i32x4_shift_left(vec, 0), (const int32_t[4]) { 1, 2, 4, 8 });
    expect_i32x4(utest_result, i32x4_shift_left(vec, 1), (const int32_t[4]) { 2, 4, 8, 16 });
    expect_i32x4(utest_result, i32x4_shift_left(vec, 4), (const int32_t[4]) { 16, 32, 64, 128 });
    expect_i32x4(utest_result, i32x4_shift_left(vec, 8), (const int32_t[4]) { 256, 512, 1024, 2048 });
}

UTEST(i32x4, shift_right_comprehensive) {
    i32x4 vec = i32x4_new(256, 512, 1024, -1024);

    // ARM NEON cannot shift right by 0, so that path checks the identity.
#if CPU_ARM64
    i32x4 shift0 = vec;
#else
    i32x4 shift0 = i32x4_shift_right(vec, 0);
#endif
    expect_i32x4(utest_result, shift0, (const int32_t[4]) { 256, 512, 1024, -1024 });
    expect_i32x4(utest_result, i32x4_shift_right(vec, 1), (const int32_t[4]) { 128, 256, 512, -512 });
    expect_i32x4(utest_result, i32x4_shift_right(vec, 4), (const int32_t[4]) { 16, 32, 64, -64 });
    expect_i32x4(utest_result, i32x4_shift_right(vec, 8), (const int32_t[4]) { 1, 2, 4, -4 });
}

UTEST(i32x4, table_shuffle) {
    i32x4 vec = i32x4_new(0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00);

    // Reverse shuffle table (each i32 is 4 bytes)
    const uint8_t table_reverse[16] = { 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3 };
    const int32_t expected[4] = { (int32_t)0xDDEEFF00, (int32_t)0x99AABBCC, 0x55667788, 0x11223344 };

    expect_i32x4(utest_result, i32x4_table_shuffle(vec, table_reverse), expected);
}

// MSVC's _Generic support has issues with SIMD intrinsic types
#if __STDC_VERSION__ >= 201112L && !COMPILER_MSVC

UTEST(generics, add_f32x4) {
    f32x4 a = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 b = f32x4_new(4.0f, 3.0f, 2.0f, 1.0f);

    expect_f32x4(utest_result, simd_add(a, b), (const float[4]) { 5.0f, 5.0f, 5.0f, 5.0f });
}

UTEST(generics, add_i32x4) {
    i32x4 a = i32x4_new(1, 2, 3, 4);
    i32x4 b = i32x4_new(4, 3, 2, 1);

    expect_i32x4(utest_result, simd_add(a, b), (const int32_t[4]) { 5, 5, 5, 5 });
}

UTEST(generics, new_splat) {
    expect_f32x4(utest_result, simd_new_splat(5.0f), (const float[4]) { 5.0f, 5.0f, 5.0f, 5.0f });
    expect_i32x4(utest_result, simd_new_splat(42), (const int32_t[4]) { 42, 42, 42, 42 });
}

UTEST(generics, extract) {
    f32x4 vec_f32 = f32x4_new(1.1f, 2.2f, 3.3f, 4.4f);
    ASSERT_NEAR(simd_extract(vec_f32, 0), 1.1f, 1e-6f);
    ASSERT_NEAR(simd_extract(vec_f32, 1), 2.2f, 1e-6f);

    i32x4 vec_i32 = i32x4_new(10, 20, 30, 40);
    ASSERT_EQ(simd_extract(vec_i32, 0), 10);
    ASSERT_EQ(simd_extract(vec_i32, 1), 20);
}

#endif // __STDC_VERSION__ >= 201112L && !COMPILER_MSVC
