#include "simd_test_support.h"

static i16x8 i16x8_half_alpha(void) {
    // t is fixed point in [0, 0x7FFF], so 0x4000 is 0.5.
    return i16x8_new_splat(0x4000);
}

static void expect_i16x8_in_range(int* utest_result, i16x8 vec, int16_t low, int16_t high) {
    int16_t result[8];
    i16x8_to_fixed_array(vec, result);
    for_count(i, 8) {
        ASSERT_GE(result[i], low);
        ASSERT_LE(result[i], high);
    }
}

UTEST(i16x8, new) {
    expect_i16x8(utest_result, i16x8_new(1, 2, 3, 4, 5, 6, 7, 8), (const int16_t[8]) { 1, 2, 3, 4, 5, 6, 7, 8 });
}

UTEST(i16x8, new_splat) {
    expect_i16x8(utest_result, i16x8_new_splat(42), (const int16_t[8]) { 42, 42, 42, 42, 42, 42, 42, 42 });
}

UTEST(i16x8, load_store_unaligned) {
    const int16_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    i16x8 vec = i16x8_load_unaligned(data, 0);

    int16_t result[8];
    i16x8_store_unaligned(vec, result, 0);

    ASSERT_TRUE(i16_arrays_equal(data, result, 8));
}

UTEST(i16x8, add) {
    i16x8 a = i16x8_new(1, 2, 3, 4, 5, 6, 7, 8);
    i16x8 b = i16x8_new(8, 7, 6, 5, 4, 3, 2, 1);

    expect_i16x8(utest_result, i16x8_add(a, b), (const int16_t[8]) { 9, 9, 9, 9, 9, 9, 9, 9 });
}

UTEST(i16x8, lerp) {
    i16x8 start = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);
    i16x8 end = i16x8_new(20, 40, 60, 80, 100, 120, 140, 160);

    // t is fixed point in [0, 0x7FFF], so 0x4000 is 0.5.
    i16x8 t = i16x8_new(0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000);

    const int16_t expected[8] = { 15, 30, 45, 60, 75, 90, 105, 120 };

    expect_i16x8(utest_result, i16x8_lerp(start, end, t), expected);
}

UTEST(i16x8, additional_functions) {
    i16x8 a = i16x8_new(0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x1000);
    i16x8 b = i16x8_new_splat(0x0800);
    i16x8 and_mask = i16x8_new_splat(0x3333);

    // Rounded high multiply: (a*b + 0x4000) >> 15, which for b == 0x0800 is an exact >> 4.
    const int16_t expected_mul[8] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x100 };
    expect_i16x8(utest_result, i16x8_mul_high(a, b), expected_mul);

    const int16_t expected_and[8] = { 0x1000, 0x2000, 0x3000, 0x0000, 0x1000, 0x2000, 0x3000, 0x1000 };
    expect_i16x8(utest_result, i16x8_and(a, and_mask), expected_and);

    const int16_t expected_shift[8] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x100 };
    expect_i16x8(utest_result, i16x8_shift_right(a, 4), expected_shift);
}

UTEST(i16x8, shift_right_comprehensive) {
    i16x8 vec = i16x8_new(256, 512, 1024, 2048, -256, -512, -1024, -2048);

    // ARM NEON cannot shift right by 0, so that path checks the identity.
#if CPU_ARM64
    i16x8 shift0 = vec;
#else
    i16x8 shift0 = i16x8_shift_right(vec, 0);
#endif
    expect_i16x8(utest_result, shift0, (const int16_t[8]) { 256, 512, 1024, 2048, -256, -512, -1024, -2048 });
    expect_i16x8(utest_result, i16x8_shift_right(vec, 1), (const int16_t[8]) { 128, 256, 512, 1024, -128, -256, -512, -1024 });
    expect_i16x8(utest_result, i16x8_shift_right(vec, 4), (const int16_t[8]) { 16, 32, 64, 128, -16, -32, -64, -128 });
    expect_i16x8(utest_result, i16x8_shift_right(vec, 8), (const int16_t[8]) { 1, 2, 4, 8, -1, -2, -4, -8 });
}

UTEST(i16x8, pack_high_to_u8x16) {
    i16x8 p0 = i16x8_new(0x0123, 0x0234, 0x0345, 0x0456, 0x0567, 0x0678, 0x0789, 0x089A);
    i16x8 p1 = i16x8_new(0x1122, 0x1233, 0x1344, 0x1455, 0x1566, 0x1677, 0x1788, 0x1899);

    const uint8_t expected[16]
        = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18 };
    expect_u8x16(utest_result, i16x8_pack_high_to_u8x16(p0, p1), expected);
}

UTEST(i16x8, table_shuffle) {
    i16x8 vec = i16x8_new(1, 2, 3, 4, 5, 6, 7, 8);

    // Reverse shuffle table (each i16 is 2 bytes)
    const uint8_t table_reverse[16] = { 14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1 };

    expect_i16x8(utest_result, i16x8_table_shuffle(vec, table_reverse), (const int16_t[8]) { 8, 7, 6, 5, 4, 3, 2, 1 });
}

UTEST(i16x8, splat_lane) {
    i16x8 vec = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);

    expect_i16x8(utest_result, i16x8_splat_lane(vec, 2), (const int16_t[8]) { 30, 30, 30, 30, 30, 30, 30, 30 });
}

UTEST(i16x8, lerp_diff) {
    i16x8 start = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);

    // Delta (end - start) precomputed
    i16x8 delta = i16x8_new(10, 20, 30, 40, 50, 60, 70, 80);

    const int16_t expected[8] = { 15, 30, 45, 60, 75, 90, 105, 120 };

    expect_i16x8(utest_result, i16x8_lerp_diff(start, delta, i16x8_half_alpha()), expected);
}

// The lerp cases below use color-blending ranges: 0-255 colors, 0-32767 alpha.

UTEST(i16x8, lerp_color_blend_full_alpha) {
    i16x8 start = i16x8_new_splat(0);
    i16x8 end = i16x8_new_splat(255);
    i16x8 t = i16x8_new_splat(0x7FFF);

    // At t = 0x7FFF, the maximum, the result is within one of end (255).
    expect_i16x8_in_range(utest_result, i16x8_lerp(start, end, t), 254, 255);
}

UTEST(i16x8, lerp_color_blend_zero_alpha) {
    i16x8 start = i16x8_new_splat(100);
    i16x8 end = i16x8_new_splat(200);
    i16x8 t = i16x8_new_splat(0);

    expect_i16x8(utest_result, i16x8_lerp(start, end, t), (const int16_t[8]) { 100, 100, 100, 100, 100, 100, 100, 100 });
}

UTEST(i16x8, lerp_color_blend_varying_alpha) {
    i16x8 start = i16x8_new(0, 50, 100, 150, 200, 250, 0, 128);
    i16x8 end = i16x8_new_splat(255);

    i16x8 t = i16x8_new(0x2000, 0x4000, 0x6000, 0x7FFF, 0x1000, 0x7000, 0x3000, 0x5000);

    i16x8 result_vec = i16x8_lerp(start, end, t);
    int16_t result[8];
    i16x8_to_fixed_array(result_vec, result);

    expect_i16x8_in_range(utest_result, result_vec, 0, 255);

    int16_t start_arr[8], end_arr[8];
    i16x8_to_fixed_array(start, start_arr);
    i16x8_to_fixed_array(end, end_arr);
    for_count(i, 8) {
        ASSERT_GE(result[i], start_arr[i]);
        ASSERT_LE(result[i], end_arr[i]);
    }
}

UTEST(i16x8, lerp_diff_color_blend) {
    i16x8 start = i16x8_new(0, 50, 100, 150, 200, 250, 0, 128);
    i16x8 delta = i16x8_new(255, 205, 155, 105, 55, 5, 255, 127);
    i16x8 t = i16x8_new(0x2000, 0x4000, 0x6000, 0x7FFF, 0x1000, 0x7000, 0x3000, 0x5000);

    expect_i16x8_in_range(utest_result, i16x8_lerp_diff(start, delta, t), 0, 255);
}

UTEST(i16x8, additional_pointer_functions) {
    const int16_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    i16x8 loaded = i16x8_load_unaligned_ptr(data, 0);
    expect_i16x8(utest_result, loaded, data);

    int16_t stored_data[8] = { 0 };
    i16x8_store_unaligned_ptr(loaded, stored_data);
    ASSERT_TRUE(i16_arrays_equal(data, stored_data, 8));

    int16_t stored_lower[8] = { 0 };
    i16x8_store_unaligned_ptr_lower(loaded, stored_lower);
    const int16_t expected_lower[8] = { 1, 2, 3, 4, 0, 0, 0, 0 };
    ASSERT_TRUE(i16_arrays_equal(expected_lower, stored_lower, 8));
}

UTEST(i16x8x2, to_u8x16_narrow_shr_shift_by_8) {
    i16x8x2 input;
    input.v0 = i16x8_new(1 << 8, 2 << 8, 3 << 8, 4 << 8, 5 << 8, 6 << 8, 7 << 8, 8 << 8);
    input.v1 = i16x8_new(9 << 8, 10 << 8, 11 << 8, 12 << 8, 13 << 8, 14 << 8, 15 << 8, 16 << 8);

    const uint8_t expected[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    expect_u8x16(utest_result, i16x8x2_pack_to_u8x16_narrow_shr(input, 8), expected);
}

UTEST(i16x8x2, to_u8x16_narrow_shr_shift_by_8_alternating) {
    i16x8x2 input;
    input.v0 = i16x8_new(0x0100, 0, 0x0100, 0, 0x0100, 0, 0x0100, 0);
    input.v1 = i16x8_new(0x0100, 0, 0x0100, 0, 0x0100, 0, 0x0100, 0);

    const uint8_t expected[16] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
    expect_u8x16(utest_result, i16x8x2_pack_to_u8x16_narrow_shr(input, 8), expected);
}

UTEST(i16x8x2, to_u8x16_narrow_shr_zero_input) {
    i16x8x2 input;
    input.v0 = i16x8_new_splat(0);
    input.v1 = i16x8_new_splat(0);

    const uint8_t expected[16] = { 0 };
    expect_u8x16(utest_result, i16x8x2_pack_to_u8x16_narrow_shr(input, 8), expected);
}

UTEST(i16x8x2, to_u8x16_narrow_shr_saturation_clamping) {
    // Values above the u8 range saturate to 255.
    i16x8x2 input;
    input.v0 = i16x8_new(1000, 2000, 4000, 8000, 1000, 1200, 1500, 2000);
    input.v1 = i16x8_new(1200, 1600, 2048, 4096, 1800, 1400, 1000, 1100);

    const uint8_t expected[16] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
    expect_u8x16(utest_result, i16x8x2_pack_to_u8x16_narrow_shr(input, 1), expected);
}

UTEST(i16x8x2, roundtrip_wide_shl_narrow_shr) {
    uint8_t original[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    u8x16 input = u8x16_new(original);

    i16x8x2 widened = u8x16_unpack_to_i16x8_wide_shl(input, 8);

    expect_u8x16(utest_result, i16x8x2_pack_to_u8x16_narrow_shr(widened, 8), original);
}
