#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/simd.h>
#include <math.h>
#include <string.h>

static bool f32_arrays_equal(const float* a, const float* b, int count, float epsilon) {
    for (int i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

static bool i32_arrays_equal(const int32_t* a, const int32_t* b, int count) {
    return memcmp(a, b, count * sizeof(int32_t)) == 0;
}

static bool i16_arrays_equal(const int16_t* a, const int16_t* b, int count) {
    return memcmp(a, b, count * sizeof(int16_t)) == 0;
}

static bool u8_arrays_equal(const uint8_t* a, const uint8_t* b, int count) {
    return memcmp(a, b, count * sizeof(uint8_t)) == 0;
}

static void f32x4_to_fixed_array(f32x4 vec, float* out) {
    out[0] = f32x4_extract(vec, 0);
    out[1] = f32x4_extract(vec, 1);
    out[2] = f32x4_extract(vec, 2);
    out[3] = f32x4_extract(vec, 3);
}

static void i32x4_to_fixed_array(i32x4 vec, int32_t* out) {
    out[0] = i32x4_extract(vec, 0);
    out[1] = i32x4_extract(vec, 1);
    out[2] = i32x4_extract(vec, 2);
    out[3] = i32x4_extract(vec, 3);
}

static void i16x8_to_fixed_array(i16x8 vec, int16_t* out) {
    out[0] = i16x8_extract(vec, 0);
    out[1] = i16x8_extract(vec, 1);
    out[2] = i16x8_extract(vec, 2);
    out[3] = i16x8_extract(vec, 3);
    out[4] = i16x8_extract(vec, 4);
    out[5] = i16x8_extract(vec, 5);
    out[6] = i16x8_extract(vec, 6);
    out[7] = i16x8_extract(vec, 7);
}

static void u16x8_to_fixed_array(u16x8 vec, uint16_t* out) {
    i16x8 signed_vec = (i16x8) { .v = vec.v };
    int16_t temp[8];
    i16x8_to_fixed_array(signed_vec, temp);
    for (int i = 0; i < 8; i++) {
        out[i] = (uint16_t)temp[i];
    }
}

static void f16x8_to_fixed_array(f16x8 vec, float* out) {
#if CPU_X64 || CPU_X86
    f32x4_store_unaligned(vec.low, out);
    f32x4_store_unaligned(vec.high, out + 4);
#elif CPU_ARM64
    __fp16 temp[8];
    vst1q_f16(temp, vec.v);
    for (int i = 0; i < 8; i++) {
        out[i] = (float)temp[i];
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, load_store_unaligned) {
    const float data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    f32x4 vec = f32x4_load_unaligned(data);

    float result[4];
    f32x4_store_unaligned(vec, result);

    ASSERT_TRUE(f32_arrays_equal(data, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, new_splat) {
    f32x4 vec = f32x4_new_splat(5.0f);
    float result[4];
    f32x4_to_fixed_array(vec, result);

    const float expected[4] = { 5.0f, 5.0f, 5.0f, 5.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, new_xy) {
    f32x4 vec = f32x4_new_xy(1.0f, 2.0f);
    float result[4];
    f32x4_to_fixed_array(vec, result);

    const float expected[4] = { 1.0f, 2.0f, 1.0f, 2.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, new) {
    f32x4 vec = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    float result[4];
    f32x4_to_fixed_array(vec, result);

    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, add) {
    f32x4 a = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 b = f32x4_new(4.0f, 3.0f, 2.0f, 1.0f);
    f32x4 result_vec = f32x4_add(a, b);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 5.0f, 5.0f, 5.0f, 5.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, sub) {
    f32x4 a = f32x4_new(5.0f, 7.0f, 9.0f, 11.0f);
    f32x4 b = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 result_vec = f32x4_sub(a, b);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 4.0f, 5.0f, 6.0f, 7.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, mul) {
    f32x4 a = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 b = f32x4_new(2.0f, 3.0f, 4.0f, 5.0f);
    f32x4 result_vec = f32x4_mul(a, b);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 2.0f, 6.0f, 12.0f, 20.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, drop_fraction) {
    f32x4 a = f32x4_new(1.9f, 2.1f, 3.7f, 4.3f);
    f32x4 result_vec = f32x4_drop_fraction(a);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, floor) {
    f32x4 a = f32x4_new(1.9f, 2.1f, 3.7f, 4.3f);
    f32x4 result_vec = f32x4_floor(a);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, sqrt) {
    f32x4 a = f32x4_new(1.0f, 4.0f, 9.0f, 16.0f);
    f32x4 result_vec = f32x4_sqrt(a);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, new_splat) {
    i32x4 vec = i32x4_new_splat(42);
    int32_t result[4];
    i32x4_to_fixed_array(vec, result);

    const int32_t expected[4] = { 42, 42, 42, 42 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, new) {
    i32x4 vec = i32x4_new(1, 2, 3, 4);
    int32_t result[4];
    i32x4_to_fixed_array(vec, result);

    const int32_t expected[4] = { 1, 2, 3, 4 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, add) {
    i32x4 a = i32x4_new(1, 2, 3, 4);
    i32x4 b = i32x4_new(4, 3, 2, 1);
    i32x4 result_vec = i32x4_add(a, b);

    int32_t result[4];
    i32x4_to_fixed_array(result_vec, result);

    const int32_t expected[4] = { 5, 5, 5, 5 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, min) {
    i32x4 a = i32x4_new(1, 4, 3, 8);
    i32x4 b = i32x4_new(2, 3, 4, 7);
    i32x4 result_vec = i32x4_min(a, b);

    int32_t result[4];
    i32x4_to_fixed_array(result_vec, result);

    const int32_t expected[4] = { 1, 3, 3, 7 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, max) {
    i32x4 a = i32x4_new(1, 4, 3, 8);
    i32x4 b = i32x4_new(2, 3, 4, 7);
    i32x4 result_vec = i32x4_max(a, b);

    int32_t result[4];
    i32x4_to_fixed_array(result_vec, result);

    const int32_t expected[4] = { 2, 4, 4, 8 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, extract) {
    i32x4 vec = i32x4_new(10, 20, 30, 40);
    ASSERT_EQ(i32x4_extract(vec, 0), 10);
    ASSERT_EQ(i32x4_extract(vec, 1), 20);
    ASSERT_EQ(i32x4_extract(vec, 2), 30);
    ASSERT_EQ(i32x4_extract(vec, 3), 40);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, div) {
    i32x4 a = i32x4_new(100, 50, 24, 16);
    i32x4 b = i32x4_new(10, 5, 6, 4);
    i32x4 result_vec = i32x4_div(a, b);

    int32_t result[4];
    i32x4_to_fixed_array(result_vec, result);

    const int32_t expected[4] = { 10, 10, 4, 4 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, div_gradient_case) {
    i32x4 numerator = i32x4_new_splat(0x8000);
    i32x4 denominator = i32x4_new(1919, 1079, 999, 499); // width-1, height-1 for common resolutions
    i32x4 result_vec = i32x4_div(numerator, denominator);

    int32_t result[4];
    i32x4_to_fixed_array(result_vec, result);

    // Expected values: 32768 / (width-1) for fixed point step calculations
    const int32_t expected[4] = { 17, 30, 33, 66 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, new) {
    i16x8 vec = i16x8_new(1, 2, 3, 4, 5, 6, 7, 8);
    int16_t result[8];
    i16x8_to_fixed_array(vec, result);

    const int16_t expected[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, new_splat) {
    i16x8 vec = i16x8_new_splat(42);
    int16_t result[8];
    i16x8_to_fixed_array(vec, result);

    const int16_t expected[8] = { 42, 42, 42, 42, 42, 42, 42, 42 };
    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, load_store_unaligned) {
    const int16_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    i16x8 vec = i16x8_load_unaligned(data, 0);

    int16_t result[8];
    i16x8_store_unaligned(vec, result, 0);

    ASSERT_TRUE(i16_arrays_equal(data, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, add) {
    i16x8 a = i16x8_new(1, 2, 3, 4, 5, 6, 7, 8);
    i16x8 b = i16x8_new(8, 7, 6, 5, 4, 3, 2, 1);
    i16x8 result_vec = i16x8_add(a, b);

    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    const int16_t expected[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, lerp) {
    i16x8 start = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);
    i16x8 end = i16x8_new(20, 40, 60, 80, 100, 120, 140, 160);

    // t values (fixed-point representation in range [0, 0x7FFF])
    // Equivalent to 0.5 in fixed-point
    i16x8 t = i16x8_new(0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000);

    const int16_t expected[8] = { 15, 30, 45, 60, 75, 90, 105, 120 };

    i16x8 result_vec = i16x8_lerp(start, end, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, new_and_store_unaligned) {
    const uint8_t data[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 simd_vec = u8x16_new(data);

    uint8_t stored_data[16];
    u8x16_store_unaligned(simd_vec, stored_data, 0);

    ASSERT_TRUE(u8_arrays_equal(data, stored_data, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, new_splat) {
    const uint8_t val = 42;
    u8x16 simd_vec = u8x16_new_splat(val);

    uint8_t result[16];
    u8x16_to_fixed_array(simd_vec, result);

    const uint8_t expected[16] = { val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val };
    ASSERT_TRUE(u8_arrays_equal(expected, result, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, load_unaligned) {
    uint8_t data_source[32];
    for (int i = 0; i < 32; i++) {
        data_source[i] = (uint8_t)i;
    }

    u8x16 simd_vec0 = u8x16_load_unaligned(data_source, 0);
    uint8_t result0[16];
    u8x16_to_fixed_array(simd_vec0, result0);

    uint8_t expected0[16];
    for (int i = 0; i < 16; i++) {
        expected0[i] = data_source[i];
    }
    ASSERT_TRUE(u8_arrays_equal(expected0, result0, 16));

    const size_t offset = 4;
    u8x16 simd_vec_offset = u8x16_load_unaligned(data_source, offset);
    uint8_t result_offset[16];
    u8x16_to_fixed_array(simd_vec_offset, result_offset);

    uint8_t expected_offset[16];
    for (int i = 0; i < 16; i++) {
        expected_offset[i] = data_source[i + offset];
    }
    ASSERT_TRUE(u8_arrays_equal(expected_offset, result_offset, 16));
}

// MSVC's _Generic support has issues with SIMD intrinsic types
#if __STDC_VERSION__ >= 201112L && !COMPILER_MSVC

UTEST(generics, add_f32x4) {
    f32x4 a = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);
    f32x4 b = f32x4_new(4.0f, 3.0f, 2.0f, 1.0f);
    f32x4 result_vec = simd_add(a, b);

    float result[4];
    f32x4_to_fixed_array(result_vec, result);

    const float expected[4] = { 5.0f, 5.0f, 5.0f, 5.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(generics, add_i32x4) {
    i32x4 a = i32x4_new(1, 2, 3, 4);
    i32x4 b = i32x4_new(4, 3, 2, 1);
    i32x4 result_vec = simd_add(a, b);

    int32_t result[4];
    i32x4_to_fixed_array(result_vec, result);

    const int32_t expected[4] = { 5, 5, 5, 5 };
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(generics, new_splat) {
    f32x4 vec_f32 = simd_new_splat(5.0f);
    float result_f32[4];
    f32x4_to_fixed_array(vec_f32, result_f32);
    const float expected_f32[4] = { 5.0f, 5.0f, 5.0f, 5.0f };
    ASSERT_TRUE(f32_arrays_equal(expected_f32, result_f32, 4, 1e-6f));

    i32x4 vec_i32 = simd_new_splat(42);
    int32_t result_i32[4];
    i32x4_to_fixed_array(vec_i32, result_i32);
    const int32_t expected_i32[4] = { 42, 42, 42, 42 };
    ASSERT_TRUE(i32_arrays_equal(expected_i32, result_i32, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(generics, extract) {
    f32x4 vec_f32 = f32x4_new(1.1f, 2.2f, 3.3f, 4.4f);
    ASSERT_NEAR(simd_extract(vec_f32, 0), 1.1f, 1e-6f);
    ASSERT_NEAR(simd_extract(vec_f32, 1), 2.2f, 1e-6f);

    i32x4 vec_i32 = i32x4_new(10, 20, 30, 40);
    ASSERT_EQ(simd_extract(vec_i32, 0), 10);
    ASSERT_EQ(simd_extract(vec_i32, 1), 20);
}

#endif // __STDC_VERSION__ >= 201112L

UTEST(f32x4, shuffle_functions) {
    f32x4 vec = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);

    f32x4 result_0101 = f32x4_shuffle(vec, vec, 0, 1, 0, 1);
    float result_0101_arr[4];
    f32x4_to_fixed_array(result_0101, result_0101_arr);
    const float expected_0101[4] = { 1.0f, 2.0f, 1.0f, 2.0f };
    ASSERT_TRUE(f32_arrays_equal(expected_0101, result_0101_arr, 4, 1e-6f));

    f32x4 result_2323 = f32x4_shuffle(vec, vec, 2, 3, 2, 3);
    float result_2323_arr[4];
    f32x4_to_fixed_array(result_2323, result_2323_arr);
    const float expected_2323[4] = { 3.0f, 4.0f, 3.0f, 4.0f };
    ASSERT_TRUE(f32_arrays_equal(expected_2323, result_2323_arr, 4, 1e-6f));

    // Test cross-vector shuffle
    f32x4 vec2 = f32x4_new(5.0f, 6.0f, 7.0f, 8.0f);
    f32x4 result_cross = f32x4_shuffle(vec, vec2, 0, 1, 4, 5);
    float result_cross_arr[4];
    f32x4_to_fixed_array(result_cross, result_cross_arr);
    const float expected_cross[4] = { 1.0f, 2.0f, 5.0f, 6.0f };
    ASSERT_TRUE(f32_arrays_equal(expected_cross, result_cross_arr, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, and_function) {
    f32x4 a = f32x4_new(1.0f, -1.0f, 2.0f, -2.0f);
    f32x4 mask = f32x4_new(1.0f, 1.0f, 0.0f, 0.0f);
    f32x4 result = f32x4_and(a, mask);

    float result_arr[4];
    f32x4_to_fixed_array(result, result_arr);
    // Bitwise AND of the IEEE-754 patterns: 1.0f (0x3F800000) survives against itself and against -1.0f
    // (0xBF800000, sign bit cleared); anything ANDed with 0.0f (all-zero) is 0.0f.
    const float expected[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result_arr, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, test_intersect) {
    f32x4 a = f32x4_new(1.0f, 1.0f, 3.0f, 3.0f);
    f32x4 b = f32x4_new(2.0f, 2.0f, 4.0f, 4.0f);
    ASSERT_TRUE(f32x4_test_intersect(a, b));

    f32x4 c = f32x4_new(1.0f, 1.0f, 2.0f, 2.0f);
    f32x4 d = f32x4_new(3.0f, 3.0f, 4.0f, 4.0f);
    ASSERT_FALSE(f32x4_test_intersect(c, d));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, extract_all_lanes) {
    f32x4 vec = f32x4_new(1.1f, 2.2f, 3.3f, 4.4f);

    ASSERT_NEAR(f32x4_extract(vec, 0), 1.1f, 1e-6f);
    ASSERT_NEAR(f32x4_extract(vec, 1), 2.2f, 1e-6f);
    ASSERT_NEAR(f32x4_extract(vec, 2), 3.3f, 1e-6f);
    ASSERT_NEAR(f32x4_extract(vec, 3), 4.4f, 1e-6f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, shift_left_comprehensive) {
    i32x4 vec = i32x4_new(1, 2, 4, 8);
    int32_t result[4];

    i32x4 shift0 = i32x4_shift_left(vec, 0);
    i32x4_to_fixed_array(shift0, result);
    const int32_t expected0[4] = { 1, 2, 4, 8 };
    ASSERT_TRUE(i32_arrays_equal(expected0, result, 4));

    i32x4 shift1 = i32x4_shift_left(vec, 1);
    i32x4_to_fixed_array(shift1, result);
    const int32_t expected1[4] = { 2, 4, 8, 16 };
    ASSERT_TRUE(i32_arrays_equal(expected1, result, 4));

    i32x4 shift4 = i32x4_shift_left(vec, 4);
    i32x4_to_fixed_array(shift4, result);
    const int32_t expected4[4] = { 16, 32, 64, 128 };
    ASSERT_TRUE(i32_arrays_equal(expected4, result, 4));

    i32x4 shift8 = i32x4_shift_left(vec, 8);
    i32x4_to_fixed_array(shift8, result);
    const int32_t expected8[4] = { 256, 512, 1024, 2048 };
    ASSERT_TRUE(i32_arrays_equal(expected8, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, shift_right_comprehensive) {
    i32x4 vec = i32x4_new(256, 512, 1024, -1024);
    int32_t result[4];

    // Test shift right by 0 - ARM NEON doesn't support 0, so test identity
#if CPU_ARM64
    i32x4 shift0 = vec;
    i32x4_to_fixed_array(shift0, result);
    const int32_t expected0[4] = { 256, 512, 1024, -1024 };
    ASSERT_TRUE(i32_arrays_equal(expected0, result, 4));
#else
    i32x4 shift0 = i32x4_shift_right(vec, 0);
    i32x4_to_fixed_array(shift0, result);
    const int32_t expected0[4] = { 256, 512, 1024, -1024 };
    ASSERT_TRUE(i32_arrays_equal(expected0, result, 4));
#endif

    i32x4 shift1 = i32x4_shift_right(vec, 1);
    i32x4_to_fixed_array(shift1, result);
    const int32_t expected1[4] = { 128, 256, 512, -512 };
    ASSERT_TRUE(i32_arrays_equal(expected1, result, 4));

    i32x4 shift4 = i32x4_shift_right(vec, 4);
    i32x4_to_fixed_array(shift4, result);
    const int32_t expected4[4] = { 16, 32, 64, -64 };
    ASSERT_TRUE(i32_arrays_equal(expected4, result, 4));

    i32x4 shift8 = i32x4_shift_right(vec, 8);
    i32x4_to_fixed_array(shift8, result);
    const int32_t expected8[4] = { 1, 2, 4, -4 };
    ASSERT_TRUE(i32_arrays_equal(expected8, result, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, additional_functions) {
    i16x8 a = i16x8_new(0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x1000);
    i16x8 b = i16x8_new(0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800);

    i16x8 mul_result = i16x8_mul_high(a, b);
    int16_t mul_arr[8];
    i16x8_to_fixed_array(mul_result, mul_arr);
    // Rounded high multiply: (a*b + 0x4000) >> 15, which for b == 0x0800 is an exact >> 4.
    const int16_t expected_mul[8] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x100 };
    ASSERT_TRUE(i16_arrays_equal(expected_mul, mul_arr, 8));

    i16x8 and_mask = i16x8_new(0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333);
    i16x8 and_result = i16x8_and(a, and_mask);
    int16_t and_arr[8];
    i16x8_to_fixed_array(and_result, and_arr);
    const int16_t expected_and[8] = { 0x1000, 0x2000, 0x3000, 0x0000, 0x1000, 0x2000, 0x3000, 0x1000 };
    ASSERT_TRUE(i16_arrays_equal(expected_and, and_arr, 8));

    i16x8 shift_result = i16x8_shift_right(a, 4);
    int16_t shift_arr[8];
    i16x8_to_fixed_array(shift_result, shift_arr);
    const int16_t expected_shift[8] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x100 };
    ASSERT_TRUE(i16_arrays_equal(expected_shift, shift_arr, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, shift_right_comprehensive) {
    i16x8 vec = i16x8_new(256, 512, 1024, 2048, -256, -512, -1024, -2048);
    int16_t result[8];

    // Test shift right by 0 - ARM NEON doesn't support 0, so test identity
#if CPU_ARM64
    i16x8 shift0 = vec;
    i16x8_to_fixed_array(shift0, result);
    const int16_t expected0[8] = { 256, 512, 1024, 2048, -256, -512, -1024, -2048 };
    ASSERT_TRUE(i16_arrays_equal(expected0, result, 8));
#else
    i16x8 shift0 = i16x8_shift_right(vec, 0);
    i16x8_to_fixed_array(shift0, result);
    const int16_t expected0[8] = { 256, 512, 1024, 2048, -256, -512, -1024, -2048 };
    ASSERT_TRUE(i16_arrays_equal(expected0, result, 8));
#endif

    i16x8 shift1 = i16x8_shift_right(vec, 1);
    i16x8_to_fixed_array(shift1, result);
    const int16_t expected1[8] = { 128, 256, 512, 1024, -128, -256, -512, -1024 };
    ASSERT_TRUE(i16_arrays_equal(expected1, result, 8));

    i16x8 shift4 = i16x8_shift_right(vec, 4);
    i16x8_to_fixed_array(shift4, result);
    const int16_t expected4[8] = { 16, 32, 64, 128, -16, -32, -64, -128 };
    ASSERT_TRUE(i16_arrays_equal(expected4, result, 8));

    i16x8 shift8 = i16x8_shift_right(vec, 8);
    i16x8_to_fixed_array(shift8, result);
    const int16_t expected8[8] = { 1, 2, 4, 8, -1, -2, -4, -8 };
    ASSERT_TRUE(i16_arrays_equal(expected8, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, pack_high_to_u8x16) {
    i16x8 p0 = i16x8_new(0x0123, 0x0234, 0x0345, 0x0456, 0x0567, 0x0678, 0x0789, 0x089A);
    i16x8 p1 = i16x8_new(0x1122, 0x1233, 0x1344, 0x1455, 0x1566, 0x1677, 0x1788, 0x1899);

    u8x16 result = i16x8_pack_high_to_u8x16(p0, p1);
    uint8_t result_arr[16];
    u8x16_to_fixed_array(result, result_arr);

    const uint8_t expected[16]
        = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18 };
    ASSERT_TRUE(u8_arrays_equal(expected, result_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, table_shuffle) {
    const uint8_t data[16] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150 };
    u8x16 simd_vec = u8x16_new(data);

    const uint8_t table_identity[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 result_identity = u8x16_table_shuffle(simd_vec, table_identity);
    uint8_t result_identity_arr[16];
    u8x16_to_fixed_array(result_identity, result_identity_arr);
    ASSERT_TRUE(u8_arrays_equal(data, result_identity_arr, 16));

    const uint8_t table_reverse[16] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    u8x16 result_reverse = u8x16_table_shuffle(simd_vec, table_reverse);
    uint8_t result_reverse_arr[16];
    u8x16_to_fixed_array(result_reverse, result_reverse_arr);

    uint8_t expected_reverse[16];
    for (int i = 0; i < 16; i++) {
        expected_reverse[i] = data[table_reverse[i]];
    }
    ASSERT_TRUE(u8_arrays_equal(expected_reverse, result_reverse_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, write_mask) {
    // Test rest_count = 5: 5 < i is true for i=6..15, false for i=0..5
    u8x16 result5 = u8x16_write_mask(5);
    uint8_t result5_arr[16];
    u8x16_to_fixed_array(result5, result5_arr);

    uint8_t expected5[16]
        = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    ASSERT_TRUE(u8_arrays_equal(expected5, result5_arr, 16));

    // Test rest_count = 0: 0 < i is true for i=1..15, false for i=0
    u8x16 result0 = u8x16_write_mask(0);
    uint8_t result0_arr[16];
    u8x16_to_fixed_array(result0, result0_arr);

    uint8_t expected0[16]
        = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    ASSERT_TRUE(u8_arrays_equal(expected0, result0_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, unpack_to_i16x8_high) {
    const uint8_t input[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 u8_vec = u8x16_new(input);

    i16x8x2 result = u8x16_unpack_to_i16x8_high(u8_vec);

    int16_t low_arr[8];
    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    i16x8_to_fixed_array(result.v1, high_arr);

    const int16_t expected_low[8] = { 0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700 };
    const int16_t expected_high[8] = { 0x0800, 0x0900, 0x0A00, 0x0B00, 0x0C00, 0x0D00, 0x0E00, 0x0F00 };

    ASSERT_TRUE(i16_arrays_equal(expected_low, low_arr, 8));
    ASSERT_TRUE(i16_arrays_equal(expected_high, high_arr, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, unpack_to_i16x8_low) {
    const uint8_t input[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 u8_vec = u8x16_new(input);

    i16x8x2 result = u8x16_unpack_to_i16x8_low(u8_vec);

    int16_t low_arr[8];
    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    i16x8_to_fixed_array(result.v1, high_arr);

    const int16_t expected_low[8] = { 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007 };
    const int16_t expected_high[8] = { 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F };

    ASSERT_TRUE(i16_arrays_equal(expected_low, low_arr, 8));
    ASSERT_TRUE(i16_arrays_equal(expected_high, high_arr, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f32x4, table_shuffle) {
    f32x4 vec = f32x4_new(1.0f, 2.0f, 3.0f, 4.0f);

    // Identity shuffle table (each f32 is 4 bytes)
    const uint8_t table_identity[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    f32x4 result_identity = f32x4_table_shuffle(vec, table_identity);

    float result_arr[4];
    f32x4_to_fixed_array(result_identity, result_arr);
    const float expected[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result_arr, 4, 1e-6f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, table_shuffle) {
    i16x8 vec = i16x8_new(1, 2, 3, 4, 5, 6, 7, 8);

    // Reverse shuffle table (each i16 is 2 bytes)
    const uint8_t table_reverse[16] = { 14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1 };
    i16x8 result_reverse = i16x8_table_shuffle(vec, table_reverse);

    int16_t result_arr[8];
    i16x8_to_fixed_array(result_reverse, result_arr);
    const int16_t expected[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
    ASSERT_TRUE(i16_arrays_equal(expected, result_arr, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, splat_lane) {
    i16x8 vec = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);

    i16x8 result_lane2 = i16x8_splat_lane(vec, 2);
    int16_t result_arr[8];
    i16x8_to_fixed_array(result_lane2, result_arr);

    const int16_t expected[8] = { 30, 30, 30, 30, 30, 30, 30, 30 };
    ASSERT_TRUE(i16_arrays_equal(expected, result_arr, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, lerp_diff) {
    i16x8 start = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);

    // Delta (end - start) precomputed
    i16x8 delta = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);

    // t values (fixed-point representation in range [0, 0x7FFF])
    // Equivalent to 0.5 in fixed-point
    i16x8 t = i16x8_new(0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000);

    const int16_t expected[8] = { 15, 30, 45, 60, 75, 90, 105, 120 };

    i16x8 result_vec = i16x8_lerp_diff(start, delta, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Verify lerp produces correct results for actual color blending ranges (0-255 colors, 0-32767 alpha).

UTEST(i16x8, lerp_color_blend_full_alpha) {
    i16x8 start = i16x8_new(0, 0, 0, 0, 0, 0, 0, 0);
    i16x8 end = i16x8_new(255, 255, 255, 255, 255, 255, 255, 255);
    i16x8 t = i16x8_new_splat(0x7FFF);

    i16x8 result_vec = i16x8_lerp(start, end, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    // At t=0x7FFF (max), result should be very close to end (255)
    for_count(i, 8) {
        ASSERT_GE(result[i], 254);
        ASSERT_LE(result[i], 255);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, lerp_color_blend_zero_alpha) {
    i16x8 start = i16x8_new(100, 100, 100, 100, 100, 100, 100, 100);
    i16x8 end = i16x8_new(200, 200, 200, 200, 200, 200, 200, 200);
    i16x8 t = i16x8_new_splat(0);

    i16x8 result_vec = i16x8_lerp(start, end, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    const int16_t expected[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, lerp_color_blend_varying_alpha) {
    i16x8 start = i16x8_new(0, 50, 100, 150, 200, 250, 0, 128);
    i16x8 end = i16x8_new(255, 255, 255, 255, 255, 255, 255, 255);

    i16x8 t = i16x8_new(0x2000, 0x4000, 0x6000, 0x7FFF, 0x1000, 0x7000, 0x3000, 0x5000);

    i16x8 result_vec = i16x8_lerp(start, end, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    for_count(i, 8) {
        ASSERT_GE(result[i], 0);
        ASSERT_LE(result[i], 255);
    }

    int16_t start_arr[8], end_arr[8];
    i16x8_to_fixed_array(start, start_arr);
    i16x8_to_fixed_array(end, end_arr);
    for_count(i, 8) {
        ASSERT_GE(result[i], start_arr[i]);
        ASSERT_LE(result[i], end_arr[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, lerp_diff_color_blend) {
    i16x8 start = i16x8_new(0, 50, 100, 150, 200, 250, 0, 128);
    i16x8 delta = i16x8_new(255, 205, 155, 105, 55, 5, 255, 127);
    i16x8 t = i16x8_new(0x2000, 0x4000, 0x6000, 0x7FFF, 0x1000, 0x7000, 0x3000, 0x5000);

    i16x8 result_vec = i16x8_lerp_diff(start, delta, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    for_count(i, 8) {
        ASSERT_GE(result[i], 0);
        ASSERT_LE(result[i], 255);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i32x4, table_shuffle) {
    i32x4 vec = i32x4_new(0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00);

    // Reverse shuffle table (each i32 is 4 bytes)
    const uint8_t table_reverse[16] = { 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3 };
    i32x4 result_reverse = i32x4_table_shuffle(vec, table_reverse);

    int32_t result_arr[4];
    i32x4_to_fixed_array(result_reverse, result_arr);
    const int32_t expected[4] = { (int32_t)0xDDEEFF00, (int32_t)0x99AABBCC, 0x55667788, 0x11223344 };
    ASSERT_TRUE(i32_arrays_equal(expected, result_arr, 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8, additional_pointer_functions) {
    const int16_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    i16x8 loaded = i16x8_load_unaligned_ptr(data, 0);
    int16_t loaded_arr[8];
    i16x8_to_fixed_array(loaded, loaded_arr);
    ASSERT_TRUE(i16_arrays_equal(data, loaded_arr, 8));

    int16_t stored_data[8] = { 0 };
    i16x8_store_unaligned_ptr(loaded, stored_data);
    ASSERT_TRUE(i16_arrays_equal(data, stored_data, 8));

    int16_t stored_lower[8] = { 0 };
    i16x8_store_unaligned_ptr_lower(loaded, stored_lower);
    const int16_t expected_lower[8] = { 1, 2, 3, 4, 0, 0, 0, 0 };
    ASSERT_TRUE(i16_arrays_equal(expected_lower, stored_lower, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, new_splat) {
    f16x8 vec = f16x8_new_splat(2.5f);
    float result[8];
    f16x8_to_fixed_array(vec, result);

    const float expected[8] = { 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f)); // f16 has lower precision
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, new) {
    f16x8 vec = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    float result[8];
    f16x8_to_fixed_array(vec, result);

    const float expected[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f)); // f16 has lower precision
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, add) {
    f16x8 a = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    f16x8 b = f16x8_new(8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f);
    f16x8 result_vec = f16x8_add(a, b);

    float result[8];
    f16x8_to_fixed_array(result_vec, result);

    const float expected[8] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, sub) {
    f16x8 a = f16x8_new(10.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 22.0f, 24.0f);
    f16x8 b = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    f16x8 result_vec = f16x8_sub(a, b);

    float result[8];
    f16x8_to_fixed_array(result_vec, result);

    const float expected[8] = { 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, mul) {
    f16x8 a = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    f16x8 b = f16x8_new(2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f);
    f16x8 result_vec = f16x8_mul(a, b);

    float result[8];
    f16x8_to_fixed_array(result_vec, result);

    const float expected[8] = { 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, clamp) {
    f16x8 a = f16x8_new(-2.0f, 1.0f, 5.0f, 8.0f, 0.5f, 12.0f, -1.0f, 3.0f);
    f16x8 min_val = f16x8_new_splat(0.0f);
    f16x8 max_val = f16x8_new_splat(10.0f);
    f16x8 result_vec = f16x8_clamp(a, min_val, max_val);

    float result[8];
    f16x8_to_fixed_array(result_vec, result);

    const float expected[8] = { 0.0f, 1.0f, 5.0f, 8.0f, 0.5f, 10.0f, 0.0f, 3.0f };
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, as_i16x8) {
    f16x8 a = f16x8_new(100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f);
    i16x8 result_vec = f16x8_as_i16x8(a);

    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    const int16_t expected[8] = { 100, 200, 300, 400, 500, 600, 700, 800 };
    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, extract_parts_as_f32x4) {
    f16x8 vec = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);

    f32x4 low_part = f16x8_extract_low_as_f32x4(vec);
    f32x4 high_part = f16x8_extract_high_as_f32x4(vec);

    float low_result[4], high_result[4];
    f32x4_to_fixed_array(low_part, low_result);
    f32x4_to_fixed_array(high_part, high_result);

    const float expected_low[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    const float expected_high[4] = { 5.0f, 6.0f, 7.0f, 8.0f };

    ASSERT_TRUE(f32_arrays_equal(expected_low, low_result, 4, 1e-3f));
    ASSERT_TRUE(f32_arrays_equal(expected_high, high_result, 4, 1e-3f));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, to_i16x8_wide_shl_shift_by_0) {
    uint8_t test_data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    u8x16 input = u8x16_new(test_data);

    i16x8x2 result = u8x16_unpack_to_i16x8_wide_shl(input, 0);

    int16_t low_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    for (int i = 0; i < 8; i++) {
        int16_t expected = test_data[i];
        ASSERT_EQ(expected, low_arr[i]);
    }

    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v1, high_arr);
    for (int i = 0; i < 8; i++) {
        int16_t expected = test_data[i + 8];
        ASSERT_EQ(expected, high_arr[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, to_i16x8_wide_shl_shift_by_8) {
    uint8_t test_data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    u8x16 input = u8x16_new(test_data);

    i16x8x2 result = u8x16_unpack_to_i16x8_wide_shl(input, 8);

    int16_t low_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    for (int i = 0; i < 8; i++) {
        int16_t expected = (int16_t)(test_data[i] << 8);
        ASSERT_EQ(expected, low_arr[i]);
    }

    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v1, high_arr);
    for (int i = 0; i < 8; i++) {
        int16_t expected = (int16_t)(test_data[i + 8] << 8);
        ASSERT_EQ(expected, high_arr[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, to_i16x8_wide_shl_shift_by_15) {
    uint8_t test_data[16] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
    u8x16 input = u8x16_new(test_data);

    i16x8x2 result = u8x16_unpack_to_i16x8_wide_shl(input, 15);

    int16_t low_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    for (int i = 0; i < 8; i++) {
        int16_t expected = (int16_t)(test_data[i] << 15);
        ASSERT_EQ(expected, low_arr[i]);
    }

    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v1, high_arr);
    for (int i = 0; i < 8; i++) {
        int16_t expected = (int16_t)(test_data[i + 8] << 15);
        ASSERT_EQ(expected, high_arr[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, to_i16x8_wide_shl_zero_input) {
    uint8_t test_data[16] = { 0 };
    u8x16 input = u8x16_new(test_data);

    i16x8x2 result = u8x16_unpack_to_i16x8_wide_shl(input, 15);

    int16_t low_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v1, high_arr);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(0, low_arr[i]);
        ASSERT_EQ(0, high_arr[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16, to_i16x8_wide_shl_max_input_values) {
    uint8_t test_data[16];
    for (int i = 0; i < 16; i++) {
        test_data[i] = 255;
    }
    u8x16 input = u8x16_new(test_data);

    i16x8x2 result = u8x16_unpack_to_i16x8_wide_shl(input, 7); // 255 << 7 = 32640, fits in i16

    int16_t expected = (int16_t)(255 << 7);
    int16_t low_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v1, high_arr);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(expected, low_arr[i]);
        ASSERT_EQ(expected, high_arr[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8x2, to_u8x16_narrow_shr_shift_by_8) {
    i16x8x2 input;
    input.v0 = i16x8_new(1 << 8, 2 << 8, 3 << 8, 4 << 8, 5 << 8, 6 << 8, 7 << 8, 8 << 8);
    input.v1 = i16x8_new(9 << 8, 10 << 8, 11 << 8, 12 << 8, 13 << 8, 14 << 8, 15 << 8, 16 << 8);

    u8x16 result = i16x8x2_pack_to_u8x16_narrow_shr(input, 8);

    uint8_t result_arr[16];
    u8x16_to_fixed_array(result, result_arr);

    uint8_t expected[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    ASSERT_TRUE(u8_arrays_equal(expected, result_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8x2, to_u8x16_narrow_shr_shift_by_8_alternating) {
    i16x8x2 input;
    input.v0 = i16x8_new(0x0100, 0, 0x0100, 0, 0x0100, 0, 0x0100, 0);
    input.v1 = i16x8_new(0x0100, 0, 0x0100, 0, 0x0100, 0, 0x0100, 0);

    u8x16 result = i16x8x2_pack_to_u8x16_narrow_shr(input, 8);

    uint8_t result_arr[16];
    u8x16_to_fixed_array(result, result_arr);

    uint8_t expected[16] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
    ASSERT_TRUE(u8_arrays_equal(expected, result_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8x2, to_u8x16_narrow_shr_zero_input) {
    i16x8x2 input;
    input.v0 = i16x8_new(0, 0, 0, 0, 0, 0, 0, 0);
    input.v1 = i16x8_new(0, 0, 0, 0, 0, 0, 0, 0);

    u8x16 result = i16x8x2_pack_to_u8x16_narrow_shr(input, 8);

    uint8_t result_arr[16];
    u8x16_to_fixed_array(result, result_arr);

    uint8_t expected[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    ASSERT_TRUE(u8_arrays_equal(expected, result_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8x2, to_u8x16_narrow_shr_saturation_clamping) {
    // Test saturation - values that would exceed u8 range should be clamped to 255
    i16x8x2 input;
    input.v0 = i16x8_new(1000, 2000, 4000, 8000, 1000, 1200, 1500, 2000);
    input.v1 = i16x8_new(1200, 1600, 2048, 4096, 1800, 1400, 1000, 1100);

    u8x16 result = i16x8x2_pack_to_u8x16_narrow_shr(input, 1);

    uint8_t result_arr[16];
    u8x16_to_fixed_array(result, result_arr);

    uint8_t expected[16] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
    ASSERT_TRUE(u8_arrays_equal(expected, result_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(i16x8x2, roundtrip_wide_shl_narrow_shr) {
    uint8_t original[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    u8x16 input = u8x16_new(original);

    i16x8x2 widened = u8x16_unpack_to_i16x8_wide_shl(input, 8);

    u8x16 result = i16x8x2_pack_to_u8x16_narrow_shr(widened, 8);

    uint8_t result_arr[16];
    u8x16_to_fixed_array(result, result_arr);

    ASSERT_TRUE(u8_arrays_equal(original, result_arr, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16_mul_widen, basic_multiplication) {
    uint8_t a_data[16] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
    uint8_t b_data[16] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };

    u8x16 a = u8x16_new(a_data);
    u8x16 b = u8x16_new(b_data);

    u16x8x2 result = u8x16_mul_widen(a, b);

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(6, low[i]);
        ASSERT_EQ(6, high[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16_mul_widen, sequential_values) {
    uint8_t a_data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    uint8_t b_data[16] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };

    u8x16 a = u8x16_new(a_data);
    u8x16 b = u8x16_new(b_data);

    u16x8x2 result = u8x16_mul_widen(a, b);

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    uint16_t expected_low[8] = { 2, 4, 6, 8, 10, 12, 14, 16 };
    uint16_t expected_high[8] = { 18, 20, 22, 24, 26, 28, 30, 32 };

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(expected_low[i], low[i]);
        ASSERT_EQ(expected_high[i], high[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16_mul_widen, max_values) {
    // Test with maximum u8 values: 255 * 255 = 65025 (fits in u16)
    uint8_t a_data[16], b_data[16];
    for (int i = 0; i < 16; i++) {
        a_data[i] = 255;
        b_data[i] = 255;
    }

    u8x16 a = u8x16_new(a_data);
    u8x16 b = u8x16_new(b_data);

    u16x8x2 result = u8x16_mul_widen(a, b);

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(65025, low[i]);
        ASSERT_EQ(65025, high[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16_mul_widen, zero_multiplication) {
    uint8_t a_data[16] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160 };
    uint8_t b_data[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    u8x16 a = u8x16_new(a_data);
    u8x16 b = u8x16_new(b_data);

    u16x8x2 result = u8x16_mul_widen(a, b);

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(0, low[i]);
        ASSERT_EQ(0, high[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16_mul_widen, identity_multiplication) {
    uint8_t a_data[16] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160 };
    uint8_t b_data[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    u8x16 a = u8x16_new(a_data);
    u8x16 b = u8x16_new(b_data);

    u16x8x2 result = u8x16_mul_widen(a, b);

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    uint16_t expected_low[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    uint16_t expected_high[8] = { 90, 100, 110, 120, 130, 140, 150, 160 };

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(expected_low[i], low[i]);
        ASSERT_EQ(expected_high[i], high[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u8x16_mul_widen, premultiply_alpha_pattern) {
    // RGBA pixels: (255,128,64,128), (200,100,50,200), (100,50,25,100), (50,25,12,50)
    uint8_t rgba_data[16] = { 255, 128, 64, 128, 200, 100, 50, 200, 100, 50, 25, 100, 50, 25, 12, 50 };
    uint8_t alpha_data[16] = { 128, 128, 128, 128, 200, 200, 200, 200, 100, 100, 100, 100, 50, 50, 50, 50 };

    u8x16 rgba = u8x16_new(rgba_data);
    u8x16 alpha = u8x16_new(alpha_data);

    u16x8x2 result = u8x16_mul_widen(rgba, alpha);

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    uint16_t expected_low[8] = {
        255 * 128, 128 * 128, 64 * 128, 128 * 128, // First pixel
        40000,     20000,     10000,    40000      // Second pixel (200*200=40000, etc)
    };
    uint16_t expected_high[8] = {
        10000, 5000, 2500, 10000, // Third pixel
        2500,  1250, 600,  2500   // Fourth pixel
    };

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(expected_low[i], low[i]);
        ASSERT_EQ(expected_high[i], high[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(f16x8, broadcast) {
    f16x8 result = f16x8_new_splat(3.14f);

    f32x4 low = f16x8_extract_low_as_f32x4(result);
    f32x4 high = f16x8_extract_high_as_f32x4(result);

    float low_arr[4], high_arr[4];
    f32x4_to_fixed_array(low, low_arr);
    f32x4_to_fixed_array(high, high_arr);

    const float epsilon = 0.01f; // f16 has lower precision
    for (int i = 0; i < 4; i++) {
        ASSERT_NEAR(low_arr[i], 3.14f, epsilon);
        ASSERT_NEAR(high_arr[i], 3.14f, epsilon);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool u16_arrays_equal(const uint16_t* a, const uint16_t* b, int count) {
    return memcmp(a, b, count * sizeof(uint16_t)) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, load_unaligned) {
    const uint16_t data[8] = { 100, 200, 300, 400, 500, 600, 700, 800 };
    u16x8 vec = u16x8_load_unaligned(data, 0);

    uint16_t result[8];
    u16x8_to_fixed_array(vec, result);

    ASSERT_TRUE(u16_arrays_equal(data, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, load_unaligned_with_offset) {
    const uint16_t data[12] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };
    u16x8 vec = u16x8_load_unaligned(data, 2);

    uint16_t result[8];
    u16x8_to_fixed_array(vec, result);

    const uint16_t expected[8] = { 30, 40, 50, 60, 70, 80, 90, 100 };
    ASSERT_TRUE(u16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, new_splat) {
    u16x8 vec = u16x8_new_splat(12345);

    uint16_t result[8];
    u16x8_to_fixed_array(vec, result);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(result[i], 12345);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, new_splat_max_value) {
    u16x8 vec = u16x8_new_splat(65535);

    uint16_t result[8];
    u16x8_to_fixed_array(vec, result);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(result[i], 65535);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, sub_basic) {
    const uint16_t a_data[8] = { 100, 200, 300, 400, 500, 600, 700, 800 };
    const uint16_t b_data[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };

    u16x8 a = u16x8_load_unaligned(a_data, 0);
    u16x8 b = u16x8_load_unaligned(b_data, 0);
    u16x8 result_vec = u16x8_sub(a, b);

    uint16_t result[8];
    u16x8_to_fixed_array(result_vec, result);

    const uint16_t expected[8] = { 90, 180, 270, 360, 450, 540, 630, 720 };
    ASSERT_TRUE(u16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, sub_with_splat) {
    const uint16_t data[8] = { 100, 150, 200, 250, 300, 350, 400, 450 };

    u16x8 vec = u16x8_load_unaligned(data, 0);
    u16x8 offset = u16x8_new_splat(100);
    u16x8 result_vec = u16x8_sub(vec, offset);

    uint16_t result[8];
    u16x8_to_fixed_array(result_vec, result);

    const uint16_t expected[8] = { 0, 50, 100, 150, 200, 250, 300, 350 };
    ASSERT_TRUE(u16_arrays_equal(expected, result, 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, hmin_basic) {
    const uint16_t data[8] = { 50, 30, 80, 20, 90, 10, 70, 40 };
    u16x8 vec = u16x8_load_unaligned(data, 0);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, 10);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, hmin_first_element) {
    const uint16_t data[8] = { 5, 30, 80, 20, 90, 10, 70, 40 };
    u16x8 vec = u16x8_load_unaligned(data, 0);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, 5);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, hmin_last_element) {
    const uint16_t data[8] = { 50, 30, 80, 20, 90, 10, 70, 3 };
    u16x8 vec = u16x8_load_unaligned(data, 0);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, 3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, hmin_all_same) {
    u16x8 vec = u16x8_new_splat(42);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, 42);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, hmin_large_values) {
    const uint16_t data[8] = { 300, 280, 350, 277, 400, 290, 310, 295 };
    u16x8 vec = u16x8_load_unaligned(data, 0);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, 277);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, narrow_to_u8x16) {
    const uint16_t a_data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    const uint16_t b_data[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };

    u16x8 a = u16x8_load_unaligned(a_data, 0);
    u16x8 b = u16x8_load_unaligned(b_data, 0);
    u8x16 result_vec = u16x8_narrow_to_u8x16(a, b);

    uint8_t result[16];
    u8x16_to_fixed_array(result_vec, result);

    const uint8_t expected[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    ASSERT_TRUE(u8_arrays_equal(expected, result, 16));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(u16x8, shuffle_mask_pattern) {
    // The pattern used in image scaling: load source indices, find min, subtract min to get shuffle offsets.
    const uint16_t src_indices[8] = { 277, 278, 278, 279, 280, 280, 281, 282 };

    u16x8 indices = u16x8_load_unaligned(src_indices, 0);
    uint16_t min_idx = u16x8_hmin(indices);
    ASSERT_EQ(min_idx, 277);

    u16x8 min_splat = u16x8_new_splat(min_idx);
    u16x8 offsets = u16x8_sub(indices, min_splat);

    uint16_t offset_values[8];
    u16x8_to_fixed_array(offsets, offset_values);

    ASSERT_EQ(offset_values[0], 0);
    ASSERT_EQ(offset_values[1], 1);
    ASSERT_EQ(offset_values[2], 1);
    ASSERT_EQ(offset_values[3], 2);
    ASSERT_EQ(offset_values[4], 3);
    ASSERT_EQ(offset_values[5], 3);
    ASSERT_EQ(offset_values[6], 4);
    ASSERT_EQ(offset_values[7], 5);

    u16x8 zero = u16x8_new_splat(0);
    u8x16 shuffle_mask = u16x8_narrow_to_u8x16(offsets, zero);

    uint8_t mask_values[16];
    u8x16_to_fixed_array(shuffle_mask, mask_values);

    // First 8 bytes should be the offsets, remaining should be 0
    ASSERT_EQ(mask_values[0], 0);
    ASSERT_EQ(mask_values[1], 1);
    ASSERT_EQ(mask_values[2], 1);
    ASSERT_EQ(mask_values[3], 2);
    ASSERT_EQ(mask_values[4], 3);
    ASSERT_EQ(mask_values[5], 3);
    ASSERT_EQ(mask_values[6], 4);
    ASSERT_EQ(mask_values[7], 5);
    for (int i = 8; i < 16; i++) {
        ASSERT_EQ(mask_values[i], 0);
    }
}
