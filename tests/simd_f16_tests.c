#include "simd_test_support.h"

static const float k_f16_ramp[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };

UTEST(f16x8, new_splat) {
    // f16 has lower precision
    expect_f16x8(utest_result, f16x8_new_splat(2.5f), (const float[8]) { 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f, 2.5f });
}

UTEST(f16x8, new) {
    // f16 has lower precision
    expect_f16x8(utest_result, f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f), k_f16_ramp);
}

UTEST(f16x8, add) {
    f16x8 a = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    f16x8 b = f16x8_new(8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f);

    expect_f16x8(utest_result, f16x8_add(a, b), (const float[8]) { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f });
}

UTEST(f16x8, sub) {
    f16x8 a = f16x8_new(10.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 22.0f, 24.0f);
    f16x8 b = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);

    expect_f16x8(utest_result, f16x8_sub(a, b), (const float[8]) { 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f });
}

UTEST(f16x8, mul) {
    f16x8 a = f16x8_new(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    f16x8 b = f16x8_new(2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f);

    expect_f16x8(utest_result, f16x8_mul(a, b), (const float[8]) { 2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f });
}

UTEST(f16x8, clamp) {
    f16x8 a = f16x8_new(-2.0f, 1.0f, 5.0f, 8.0f, 0.5f, 12.0f, -1.0f, 3.0f);
    f16x8 min_val = f16x8_new_splat(0.0f);
    f16x8 max_val = f16x8_new_splat(10.0f);

    expect_f16x8(utest_result, f16x8_clamp(a, min_val, max_val),
                 (const float[8]) { 0.0f, 1.0f, 5.0f, 8.0f, 0.5f, 10.0f, 0.0f, 3.0f });
}

UTEST(f16x8, as_i16x8) {
    f16x8 a = f16x8_new(100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f);

    expect_i16x8(utest_result, f16x8_as_i16x8(a), (const int16_t[8]) { 100, 200, 300, 400, 500, 600, 700, 800 });
}

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
