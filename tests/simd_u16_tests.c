#include "simd_test_support.h"

static void expect_hmin(int* utest_result, const uint16_t* data, uint16_t want) {
    u16x8 vec = u16x8_load_unaligned(data, 0);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, want);
}

UTEST(u16x8, load_unaligned) {
    const uint16_t data[8] = { 100, 200, 300, 400, 500, 600, 700, 800 };

    expect_u16x8(utest_result, u16x8_load_unaligned(data, 0), data);
}

UTEST(u16x8, load_unaligned_with_offset) {
    const uint16_t data[12] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120 };

    expect_u16x8(utest_result, u16x8_load_unaligned(data, 2), (const uint16_t[8]) { 30, 40, 50, 60, 70, 80, 90, 100 });
}

UTEST(u16x8, new_splat) {
    uint16_t result[8];
    u16x8_to_fixed_array(u16x8_new_splat(12345), result);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(result[i], 12345);
    }
}

UTEST(u16x8, new_splat_max_value) {
    uint16_t result[8];
    u16x8_to_fixed_array(u16x8_new_splat(65535), result);

    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(result[i], 65535);
    }
}

UTEST(u16x8, sub_basic) {
    const uint16_t a_data[8] = { 100, 200, 300, 400, 500, 600, 700, 800 };
    const uint16_t b_data[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };

    u16x8 a = u16x8_load_unaligned(a_data, 0);
    u16x8 b = u16x8_load_unaligned(b_data, 0);

    expect_u16x8(utest_result, u16x8_sub(a, b), (const uint16_t[8]) { 90, 180, 270, 360, 450, 540, 630, 720 });
}

UTEST(u16x8, sub_with_splat) {
    const uint16_t data[8] = { 100, 150, 200, 250, 300, 350, 400, 450 };

    u16x8 vec = u16x8_load_unaligned(data, 0);
    u16x8 offset = u16x8_new_splat(100);

    expect_u16x8(utest_result, u16x8_sub(vec, offset), (const uint16_t[8]) { 0, 50, 100, 150, 200, 250, 300, 350 });
}

UTEST(u16x8, hmin_basic) {
    const uint16_t data[8] = { 50, 30, 80, 20, 90, 10, 70, 40 };

    expect_hmin(utest_result, data, 10);
}

UTEST(u16x8, hmin_first_element) {
    const uint16_t data[8] = { 5, 30, 80, 20, 90, 10, 70, 40 };

    expect_hmin(utest_result, data, 5);
}

UTEST(u16x8, hmin_last_element) {
    const uint16_t data[8] = { 50, 30, 80, 20, 90, 10, 70, 3 };

    expect_hmin(utest_result, data, 3);
}

UTEST(u16x8, hmin_all_same) {
    u16x8 vec = u16x8_new_splat(42);

    uint16_t min_val = u16x8_hmin(vec);

    ASSERT_EQ(min_val, 42);
}

UTEST(u16x8, hmin_large_values) {
    const uint16_t data[8] = { 300, 280, 350, 277, 400, 290, 310, 295 };

    expect_hmin(utest_result, data, 277);
}

UTEST(u16x8, narrow_to_u8x16) {
    const uint16_t a_data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    const uint16_t b_data[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };

    u16x8 a = u16x8_load_unaligned(a_data, 0);
    u16x8 b = u16x8_load_unaligned(b_data, 0);

    const uint8_t expected[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    expect_u8x16(utest_result, u16x8_narrow_to_u8x16(a, b), expected);
}

UTEST(u16x8, shuffle_mask_pattern) {
    // The pattern used in image scaling: load source indices, find min, subtract min to get shuffle offsets.
    const uint16_t src_indices[8] = { 277, 278, 278, 279, 280, 280, 281, 282 };

    u16x8 indices = u16x8_load_unaligned(src_indices, 0);
    uint16_t min_idx = u16x8_hmin(indices);
    ASSERT_EQ(min_idx, 277);

    u16x8 min_splat = u16x8_new_splat(min_idx);
    u16x8 offsets = u16x8_sub(indices, min_splat);

    expect_u16x8(utest_result, offsets, (const uint16_t[8]) { 0, 1, 1, 2, 3, 3, 4, 5 });

    u16x8 zero = u16x8_new_splat(0);
    u8x16 shuffle_mask = u16x8_narrow_to_u8x16(offsets, zero);

    // The first 8 bytes are the offsets and the rest are 0.
    const uint8_t expected_mask[16] = { 0, 1, 1, 2, 3, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0 };
    expect_u8x16(utest_result, shuffle_mask, expected_mask);
}
