#pragma once

#include "utest/utest.h"
#include <core/core.h>
#include <core/simd.h>
#include <math.h>
#include <string.h>

static inline bool f32_arrays_equal(const float* a, const float* b, int count, float epsilon) {
    for (int i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

static inline bool i32_arrays_equal(const int32_t* a, const int32_t* b, int count) {
    return memcmp(a, b, count * sizeof(int32_t)) == 0;
}

static inline bool i16_arrays_equal(const int16_t* a, const int16_t* b, int count) {
    return memcmp(a, b, count * sizeof(int16_t)) == 0;
}

static inline bool u8_arrays_equal(const uint8_t* a, const uint8_t* b, int count) {
    return memcmp(a, b, count * sizeof(uint8_t)) == 0;
}

static inline bool u16_arrays_equal(const uint16_t* a, const uint16_t* b, int count) {
    return memcmp(a, b, count * sizeof(uint16_t)) == 0;
}

static inline void f32x4_to_fixed_array(f32x4 vec, float* out) {
    out[0] = f32x4_extract(vec, 0);
    out[1] = f32x4_extract(vec, 1);
    out[2] = f32x4_extract(vec, 2);
    out[3] = f32x4_extract(vec, 3);
}

static inline void i32x4_to_fixed_array(i32x4 vec, int32_t* out) {
    out[0] = i32x4_extract(vec, 0);
    out[1] = i32x4_extract(vec, 1);
    out[2] = i32x4_extract(vec, 2);
    out[3] = i32x4_extract(vec, 3);
}

static inline void i16x8_to_fixed_array(i16x8 vec, int16_t* out) {
    out[0] = i16x8_extract(vec, 0);
    out[1] = i16x8_extract(vec, 1);
    out[2] = i16x8_extract(vec, 2);
    out[3] = i16x8_extract(vec, 3);
    out[4] = i16x8_extract(vec, 4);
    out[5] = i16x8_extract(vec, 5);
    out[6] = i16x8_extract(vec, 6);
    out[7] = i16x8_extract(vec, 7);
}

static inline void u16x8_to_fixed_array(u16x8 vec, uint16_t* out) {
    i16x8 signed_vec = (i16x8) { .v = vec.v };
    int16_t temp[8];
    i16x8_to_fixed_array(signed_vec, temp);
    for (int i = 0; i < 8; i++) {
        out[i] = (uint16_t)temp[i];
    }
}

static inline void f16x8_to_fixed_array(f16x8 vec, float* out) {
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

static inline void expect_f32x4(int* utest_result, f32x4 vec, const float* expected) {
    float result[4];
    f32x4_to_fixed_array(vec, result);
    ASSERT_TRUE(f32_arrays_equal(expected, result, 4, 1e-6f));
}

static inline void expect_i32x4(int* utest_result, i32x4 vec, const int32_t* expected) {
    int32_t result[4];
    i32x4_to_fixed_array(vec, result);
    ASSERT_TRUE(i32_arrays_equal(expected, result, 4));
}

static inline void expect_i16x8(int* utest_result, i16x8 vec, const int16_t* expected) {
    int16_t result[8];
    i16x8_to_fixed_array(vec, result);
    ASSERT_TRUE(i16_arrays_equal(expected, result, 8));
}

static inline void expect_u16x8(int* utest_result, u16x8 vec, const uint16_t* expected) {
    uint16_t result[8];
    u16x8_to_fixed_array(vec, result);
    ASSERT_TRUE(u16_arrays_equal(expected, result, 8));
}

static inline void expect_u8x16(int* utest_result, u8x16 vec, const uint8_t* expected) {
    uint8_t result[16];
    u8x16_to_fixed_array(vec, result);
    ASSERT_TRUE(u8_arrays_equal(expected, result, 16));
}

static inline void expect_f16x8(int* utest_result, f16x8 vec, const float* expected) {
    float result[8];
    f16x8_to_fixed_array(vec, result);
    ASSERT_TRUE(f32_arrays_equal(expected, result, 8, 1e-3f));
}
