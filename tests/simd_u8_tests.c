#include "simd_test_support.h"

static void expect_i16x8_pair(int* utest_result, i16x8x2 result, const int16_t* expected_low,
                              const int16_t* expected_high) {
    int16_t low_arr[8];
    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    i16x8_to_fixed_array(result.v1, high_arr);

    ASSERT_TRUE(i16_arrays_equal(expected_low, low_arr, 8));
    ASSERT_TRUE(i16_arrays_equal(expected_high, high_arr, 8));
}

static void expect_wide_shl_matches(int* utest_result, const uint8_t* test_data, int shift, i16x8x2 result) {
    int16_t low_arr[8];
    int16_t high_arr[8];
    i16x8_to_fixed_array(result.v0, low_arr);
    i16x8_to_fixed_array(result.v1, high_arr);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ((int16_t)(test_data[i] << shift), low_arr[i]);
    }
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ((int16_t)(test_data[i + 8] << shift), high_arr[i]);
    }
}

typedef struct MulWidenCase {
    const uint8_t* a;
    const uint8_t* b;
    const uint16_t* expected_low;
    const uint16_t* expected_high;
} MulWidenCase;

static void expect_mul_widen(int* utest_result, const MulWidenCase* c) {
    u16x8x2 result = u8x16_mul_widen(u8x16_new(c->a), u8x16_new(c->b));

    uint16_t low[8], high[8];
    u16x8_to_fixed_array(result.v0, low);
    u16x8_to_fixed_array(result.v1, high);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(c->expected_low[i], low[i]);
        ASSERT_EQ(c->expected_high[i], high[i]);
    }
}

UTEST(u8x16, new_and_store_unaligned) {
    const uint8_t data[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 simd_vec = u8x16_new(data);

    uint8_t stored_data[16];
    u8x16_store_unaligned(simd_vec, stored_data, 0);

    ASSERT_TRUE(u8_arrays_equal(data, stored_data, 16));
}

UTEST(u8x16, new_splat) {
    const uint8_t val = 42;
    u8x16 simd_vec = u8x16_new_splat(val);

    const uint8_t expected[16] = { val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val };
    expect_u8x16(utest_result, simd_vec, expected);
}

UTEST(u8x16, load_unaligned) {
    uint8_t data_source[32];
    for (int i = 0; i < 32; i++) {
        data_source[i] = (uint8_t)i;
    }

    u8x16 simd_vec0 = u8x16_load_unaligned(data_source, 0);

    uint8_t expected0[16];
    for (int i = 0; i < 16; i++) {
        expected0[i] = data_source[i];
    }
    expect_u8x16(utest_result, simd_vec0, expected0);

    const size_t offset = 4;
    u8x16 simd_vec_offset = u8x16_load_unaligned(data_source, offset);

    uint8_t expected_offset[16];
    for (int i = 0; i < 16; i++) {
        expected_offset[i] = data_source[i + offset];
    }
    expect_u8x16(utest_result, simd_vec_offset, expected_offset);
}

UTEST(u8x16, table_shuffle) {
    const uint8_t data[16] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150 };
    u8x16 simd_vec = u8x16_new(data);

    const uint8_t table_identity[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    expect_u8x16(utest_result, u8x16_table_shuffle(simd_vec, table_identity), data);

    const uint8_t table_reverse[16] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    u8x16 result_reverse = u8x16_table_shuffle(simd_vec, table_reverse);

    uint8_t expected_reverse[16];
    for (int i = 0; i < 16; i++) {
        expected_reverse[i] = data[table_reverse[i]];
    }
    expect_u8x16(utest_result, result_reverse, expected_reverse);
}

UTEST(u8x16, write_mask) {
    // rest_count = 5: 5 < i is true for i=6..15, false for i=0..5
    uint8_t expected5[16]
        = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    expect_u8x16(utest_result, u8x16_write_mask(5), expected5);

    // rest_count = 0: 0 < i is true for i=1..15, false for i=0
    uint8_t expected0[16]
        = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    expect_u8x16(utest_result, u8x16_write_mask(0), expected0);
}

UTEST(u8x16, unpack_to_i16x8_high) {
    const uint8_t input[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

    const int16_t expected_low[8] = { 0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700 };
    const int16_t expected_high[8] = { 0x0800, 0x0900, 0x0A00, 0x0B00, 0x0C00, 0x0D00, 0x0E00, 0x0F00 };

    expect_i16x8_pair(utest_result, u8x16_unpack_to_i16x8_high(u8x16_new(input)), expected_low, expected_high);
}

UTEST(u8x16, unpack_to_i16x8_low) {
    const uint8_t input[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

    const int16_t expected_low[8] = { 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007 };
    const int16_t expected_high[8] = { 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F };

    expect_i16x8_pair(utest_result, u8x16_unpack_to_i16x8_low(u8x16_new(input)), expected_low, expected_high);
}

UTEST(u8x16, to_i16x8_wide_shl_shift_by_0) {
    uint8_t test_data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

    expect_wide_shl_matches(utest_result, test_data, 0, u8x16_unpack_to_i16x8_wide_shl(u8x16_new(test_data), 0));
}

UTEST(u8x16, to_i16x8_wide_shl_shift_by_8) {
    uint8_t test_data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

    expect_wide_shl_matches(utest_result, test_data, 8, u8x16_unpack_to_i16x8_wide_shl(u8x16_new(test_data), 8));
}

UTEST(u8x16, to_i16x8_wide_shl_shift_by_15) {
    uint8_t test_data[16] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };

    expect_wide_shl_matches(utest_result, test_data, 15, u8x16_unpack_to_i16x8_wide_shl(u8x16_new(test_data), 15));
}

UTEST(u8x16, to_i16x8_wide_shl_zero_input) {
    uint8_t test_data[16] = { 0 };

    expect_wide_shl_matches(utest_result, test_data, 15, u8x16_unpack_to_i16x8_wide_shl(u8x16_new(test_data), 15));
}

UTEST(u8x16, to_i16x8_wide_shl_max_input_values) {
    uint8_t test_data[16];
    for (int i = 0; i < 16; i++) {
        test_data[i] = 255;
    }

    // 255 << 7 = 32640, fits in i16
    expect_wide_shl_matches(utest_result, test_data, 7, u8x16_unpack_to_i16x8_wide_shl(u8x16_new(test_data), 7));
}

UTEST(u8x16_mul_widen, basic_multiplication) {
    const uint8_t a[16] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
    const uint8_t b[16] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    const uint16_t expected[8] = { 6, 6, 6, 6, 6, 6, 6, 6 };

    expect_mul_widen(utest_result, &(MulWidenCase) { a, b, expected, expected });
}

UTEST(u8x16_mul_widen, sequential_values) {
    const uint8_t a[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    const uint8_t b[16] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
    const uint16_t expected_low[8] = { 2, 4, 6, 8, 10, 12, 14, 16 };
    const uint16_t expected_high[8] = { 18, 20, 22, 24, 26, 28, 30, 32 };

    expect_mul_widen(utest_result, &(MulWidenCase) { a, b, expected_low, expected_high });
}

UTEST(u8x16_mul_widen, max_values) {
    // 255 * 255 = 65025, which fits in u16.
    uint8_t a[16], b[16];
    for (int i = 0; i < 16; i++) {
        a[i] = 255;
        b[i] = 255;
    }
    const uint16_t expected[8] = { 65025, 65025, 65025, 65025, 65025, 65025, 65025, 65025 };

    expect_mul_widen(utest_result, &(MulWidenCase) { a, b, expected, expected });
}

UTEST(u8x16_mul_widen, zero_multiplication) {
    const uint8_t a[16] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160 };
    const uint8_t b[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const uint16_t expected[8] = { 0 };

    expect_mul_widen(utest_result, &(MulWidenCase) { a, b, expected, expected });
}

UTEST(u8x16_mul_widen, identity_multiplication) {
    const uint8_t a[16] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160 };
    const uint8_t b[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    const uint16_t expected_low[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    const uint16_t expected_high[8] = { 90, 100, 110, 120, 130, 140, 150, 160 };

    expect_mul_widen(utest_result, &(MulWidenCase) { a, b, expected_low, expected_high });
}

UTEST(u8x16_mul_widen, premultiply_alpha_pattern) {
    // RGBA pixels: (255,128,64,128), (200,100,50,200), (100,50,25,100), (50,25,12,50)
    const uint8_t rgba_data[16] = { 255, 128, 64, 128, 200, 100, 50, 200, 100, 50, 25, 100, 50, 25, 12, 50 };
    const uint8_t alpha_data[16] = { 128, 128, 128, 128, 200, 200, 200, 200, 100, 100, 100, 100, 50, 50, 50, 50 };

    const uint16_t expected_low[8] = {
        255 * 128, 128 * 128, 64 * 128, 128 * 128, // First pixel
        40000,     20000,     10000,    40000      // Second pixel (200*200=40000, etc)
    };
    const uint16_t expected_high[8] = {
        10000, 5000, 2500, 10000, // Third pixel
        2500,  1250, 600,  2500   // Fourth pixel
    };

    expect_mul_widen(utest_result, &(MulWidenCase) { rgba_data, alpha_data, expected_low, expected_high });
}
