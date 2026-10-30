#pragma once

#include "core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if COMPILER_MSVC
#include <intrin.h>
#elif COMPILER_GCC || COMPILER_CLANG
#if CPU_X64 || CPU_X86
#include <immintrin.h>
#elif CPU_ARM64
#include <arm_neon.h>
#endif
#endif

#if (CPU_X64 || CPU_X86) && !defined(__SSE4_2__) && !COMPILER_MSVC
#error "SSE 4.2 or higher is required for SIMD operations on x86/x64 platforms. Please compile with -msse4.2 or higher."
#endif

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// SIMD Type Definitions
// =============================================================================

#if CPU_X64 || CPU_X86
typedef struct {
    __m128 v;
} f32x4;
typedef struct {
    __m128i v;
} i16x8;
typedef struct {
    __m128i v;
} i32x4;
typedef struct {
    __m128i v;
} u8x16;
typedef struct {
    __m128i v;
} u32x4;
typedef struct {
    __m128i v;
} u16x8;
#elif CPU_ARM64
typedef struct {
    float32x4_t v;
} f32x4;
typedef struct {
    int16x8_t v;
} i16x8;
typedef struct {
    int32x4_t v;
} i32x4;
typedef struct {
    uint8x16_t v;
} u8x16;
typedef struct {
    uint32x4_t v;
} u32x4;
typedef struct {
    uint16x8_t v;
} u16x8;
#else
#error "Unsupported architecture"
#endif

typedef struct {
    i16x8 v0;
    i16x8 v1;
} i16x8x2;

typedef struct {
    u16x8 v0;
    u16x8 v1;
} u16x8x2;

// =============================================================================
// f32x4 Functions
// =============================================================================

static inline f32x4 f32x4_load_unaligned(const float* data) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_loadu_ps(data) };
#elif CPU_ARM64
    return (f32x4) { .v = vld1q_f32(data) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void f32x4_store_unaligned(f32x4 a, float* data) {
#if CPU_X64 || CPU_X86
    _mm_storeu_ps(data, a.v);
#elif CPU_ARM64
    vst1q_f32(data, a.v);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_new_splat(float a) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_set1_ps(a) };
#elif CPU_ARM64
    return (f32x4) { .v = vdupq_n_f32(a) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_new(float a, float b, float c, float d) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_set_ps(d, c, b, a) };
#elif CPU_ARM64
    float32x4_t v = vdupq_n_f32(0.0f);
    v = vsetq_lane_f32(a, v, 0);
    v = vsetq_lane_f32(b, v, 1);
    v = vsetq_lane_f32(c, v, 2);
    v = vsetq_lane_f32(d, v, 3);
    return (f32x4) { .v = v };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_new_xy(float a, float b) {
    return f32x4_new(a, b, a, b);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_add(f32x4 a, f32x4 b) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_add_ps(a.v, b.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vaddq_f32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_sub(f32x4 a, f32x4 b) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_sub_ps(a.v, b.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vsubq_f32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_mul(f32x4 a, f32x4 b) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_mul_ps(a.v, b.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vmulq_f32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_floor(f32x4 a) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_floor_ps(a.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vrndmq_f32(a.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_sqrt(f32x4 a) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_sqrt_ps(a.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vsqrtq_f32(a.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_clamp(f32x4 a, f32x4 min, f32x4 max) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_min_ps(_mm_max_ps(a.v, min.v), max.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vmaxq_f32(min.v, vminq_f32(a.v, max.v)) };
#endif
}

static inline i32x4 f32x4_as_i32x4(f32x4 a);

#if CPU_X64 || CPU_X86
// _mm_extract_ps returns the float bits as int, and takes the lane as a compile-time constant.
static inline float f32x4_extract(f32x4 a, int lane) {
    union {
        int i;
        float f;
    } u;
    switch (lane) {
        case 0:
            u.i = _mm_extract_ps(a.v, 0);
            break;
        case 1:
            u.i = _mm_extract_ps(a.v, 1);
            break;
        case 2:
            u.i = _mm_extract_ps(a.v, 2);
            break;
        case 3:
            u.i = _mm_extract_ps(a.v, 3);
            break;
        default:
            u.f = 0.0f;
            break;
    }
    return u.f;
}
#elif CPU_ARM64
#define f32x4_extract(a, lane) vgetq_lane_f32((a).v, (lane))
#endif

static inline f32x4 f32x4_drop_fraction(f32x4 a) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_cvtepi32_ps(_mm_cvttps_epi32(a.v)) };
#elif CPU_ARM64
    return (f32x4) { .v = vcvtq_f32_s32(vcvtq_s32_f32(a.v)) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_and(f32x4 a, f32x4 b) {
#if CPU_X64 || CPU_X86
    return (f32x4) { .v = _mm_and_ps(a.v, b.v) };
#elif CPU_ARM64
    return (f32x4) { .v = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a.v), vreinterpretq_u32_f32(b.v))) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shuffle f32x4 vector - Select 4 elements from two input vectors
//
// Indices 0-3 select from v0, indices 4-7 select from v1.

#define f32x4_shuffle(v0, v1, i0, i1, i2, i3) ((f32x4) { .v = __builtin_shufflevector((v0).v, (v1).v, i0, i1, i2, i3) })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f32x4 f32x4_table_shuffle(f32x4 a, const uint8_t table[16]) {
#if CPU_X64 || CPU_X86
    __m128i mask = _mm_loadu_si128((const __m128i*)table);
    return (f32x4) { .v = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(a.v), mask)) };
#elif CPU_ARM64
    uint8x16_t mask = vld1q_u8(table);
    int8x16_t result_s8 = vqtbl1q_s8(vreinterpretq_s8_f32(a.v), mask);
    return (f32x4) { .v = vreinterpretq_f32_s8(result_s8) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool f32x4_test_intersect(f32x4 a, f32x4 b) {
#if CPU_X64 || CPU_X86
    // make max.x, max.y negative
    __m128 flip_sign_0 = _mm_set_ps(-0.0f, -0.0f, 0.0f, 0.0f);
    __m128 a_flipped = _mm_xor_ps(a.v, flip_sign_0);
    __m128 b_flipped = _mm_xor_ps(b.v, flip_sign_0);

    // we have min_x, min_y,  max_x, max_y and want
    //         max_x, max_y,  min_x, min_y
    __m128 b_shuffled = _mm_shuffle_ps(b_flipped, b_flipped, _MM_SHUFFLE(1, 0, 3, 2));
    __m128 flip_sign = _mm_set1_ps(-0.0f);
    b_shuffled = _mm_xor_ps(b_shuffled, flip_sign);

    // Check overlap: compare shuffled a <= negated b
    __m128 cmp_temp = _mm_cmplt_ps(a_flipped, b_shuffled);

    // Test if all comparison results are true
    return _mm_movemask_ps(cmp_temp) == 0xF;
#elif CPU_ARM64
    // Mask that flips the signs on the last two elements (max.x, max.y)
    uint32x4_t sign_mask = vsetq_lane_u32(0x80000000, vsetq_lane_u32(0x80000000, vdupq_n_u32(0), 2), 3);

    // Flip signs on max components: a_flipped = [min_x, min_y, -max_x, -max_y]
    float32x4_t a_flipped = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a.v), sign_mask));
    float32x4_t b_flipped = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(b.v), sign_mask));

    // Shuffle b_flipped from [min_x, min_y, -max_x, -max_y] to [-max_x, -max_y, min_x, min_y]
    // Then flip all signs to get [max_x, max_y, -min_x, -min_y]
    float32x4_t b_shuffled = vextq_f32(b_flipped, b_flipped, 2);
    uint32x4_t flip_all = vdupq_n_u32(0x80000000);
    b_shuffled = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(b_shuffled), flip_all));

    // Perform the comparisons: equivalent to minA.x < maxB.x, minA.y < maxB.y, maxA.x > minB.x, maxA.y > minB.y
    uint32x4_t cmp = vcltq_f32(a_flipped, b_shuffled);

    // The lane minimum is all-ones only when every comparison passed.
    return vminvq_u32(cmp) == 0xFFFFFFFFu;
#endif
}

// =============================================================================
// i32x4 Functions
// =============================================================================

static inline i32x4 i32x4_new(int32_t a, int32_t b, int32_t c, int32_t d) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_set_epi32(d, c, b, a) };
#elif CPU_ARM64
    int32x4_t v = vdupq_n_s32(0);
    v = vsetq_lane_s32(a, v, 0);
    v = vsetq_lane_s32(b, v, 1);
    v = vsetq_lane_s32(c, v, 2);
    v = vsetq_lane_s32(d, v, 3);
    return (i32x4) { .v = v };
#endif
}

// =============================================================================
// u32x4 Functions
// =============================================================================

static inline u32x4 u32x4_new(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
#if CPU_X64 || CPU_X86
    return (u32x4) { .v = _mm_set_epi32((int32_t)d, (int32_t)c, (int32_t)b, (int32_t)a) };
#elif CPU_ARM64
    uint32x4_t v = vdupq_n_u32(0);
    v = vsetq_lane_u32(a, v, 0);
    v = vsetq_lane_u32(b, v, 1);
    v = vsetq_lane_u32(c, v, 2);
    v = vsetq_lane_u32(d, v, 3);
    return (u32x4) { .v = v };
#endif
}

// =============================================================================
// u16x8 Functions
// =============================================================================

static inline u8x16 u16x8_narrow_to_u8x16(u16x8 a, u16x8 b) {
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_packus_epi16(a.v, b.v) };
#elif CPU_ARM64
    uint8x8_t low = vmovn_u16(a.v);
    uint8x8_t high = vmovn_u16(b.v);
    return (u8x16) { .v = vcombine_u8(low, high) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u16x8 u16x8_load_unaligned(const void* data, size_t offset) {
    const u8* ptr = (const u8*)data + offset * sizeof(u16);
#if CPU_X64 || CPU_X86
    return (u16x8) { .v = _mm_loadu_si128((const __m128i*)ptr) };
#elif CPU_ARM64
    return (u16x8) { .v = vld1q_u16((const uint16_t*)ptr) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u16x8 u16x8_new_splat(uint16_t a) {
#if CPU_X64 || CPU_X86
    return (u16x8) { .v = _mm_set1_epi16((int16_t)a) };
#elif CPU_ARM64
    return (u16x8) { .v = vdupq_n_u16(a) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u16x8 u16x8_sub(u16x8 a, u16x8 b) {
#if CPU_X64 || CPU_X86
    return (u16x8) { .v = _mm_sub_epi16(a.v, b.v) };
#elif CPU_ARM64
    return (u16x8) { .v = vsubq_u16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u16 u16x8_hmin(u16x8 a) {
#if CPU_X64 || CPU_X86
    // PHMINPOSUW finds minimum of 8 u16 values and puts result in low 16 bits
    __m128i minpos = _mm_minpos_epu16(a.v);
    return (u16)_mm_extract_epi16(minpos, 0);
#elif CPU_ARM64
    return vminvq_u16(a.v);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_new_splat(int32_t a) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_set1_epi32(a) };
#elif CPU_ARM64
    return (i32x4) { .v = vdupq_n_s32(a) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_add(i32x4 a, i32x4 b) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_add_epi32(a.v, b.v) };
#elif CPU_ARM64
    return (i32x4) { .v = vaddq_s32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_sub(i32x4 a, i32x4 b) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_sub_epi32(a.v, b.v) };
#elif CPU_ARM64
    return (i32x4) { .v = vsubq_s32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_div(i32x4 a, i32x4 b) {
#if CPU_X64 || CPU_X86
    __m128 a_f = _mm_cvtepi32_ps(a.v);
    __m128 b_f = _mm_cvtepi32_ps(b.v);
    __m128 result_f = _mm_div_ps(a_f, b_f);
    return (i32x4) { .v = _mm_cvtps_epi32(result_f) };
#elif CPU_ARM64
    // vcvtnq_s32_f32 rounds to nearest, matching x86, rather than truncating
    float32x4_t a_f = vcvtq_f32_s32(a.v);
    float32x4_t b_f = vcvtq_f32_s32(b.v);
    float32x4_t result_f = vdivq_f32(a_f, b_f);
    return (i32x4) { .v = vcvtnq_s32_f32(result_f) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_min(i32x4 a, i32x4 b) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_min_epi32(a.v, b.v) };
#elif CPU_ARM64
    return (i32x4) { .v = vminq_s32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_max(i32x4 a, i32x4 b) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_max_epi32(a.v, b.v) };
#elif CPU_ARM64
    return (i32x4) { .v = vmaxq_s32(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_abs(i32x4 a) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_abs_epi32(a.v) };
#elif CPU_ARM64
    return (i32x4) { .v = vabsq_s32(a.v) };
#endif
}

#if CPU_X64 || CPU_X86
#define i32x4_extract(a, lane) _mm_extract_epi32((a).v, (lane))
#elif CPU_ARM64
#define i32x4_extract(a, lane) vgetq_lane_s32((a).v, (lane))
#endif

#if CPU_X64 || CPU_X86
#define i32x4_shift_left(a, count) ((i32x4) { .v = _mm_slli_epi32((a).v, (count)) })
#elif CPU_ARM64
#define i32x4_shift_left(a, count) ((i32x4) { .v = vshlq_n_s32((a).v, (count)) })
#endif

#if CPU_X64 || CPU_X86
#define i32x4_shift_right(a, count) ((i32x4) { .v = _mm_srai_epi32((a).v, (count)) })
#elif CPU_ARM64
#define i32x4_shift_right(a, count) ((i32x4) { .v = vshrq_n_s32((a).v, (count)) })
#endif

static inline i16x8 i32x4_pack_i16x8(i32x4 a) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_packs_epi32(a.v, a.v) };
#elif CPU_ARM64
    int16x4_t narrow = vqmovn_s32(a.v);
    return (i16x8) { .v = vcombine_s16(narrow, narrow) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i32x4 i32x4_table_shuffle(i32x4 a, const uint8_t table[16]) {
#if CPU_X64 || CPU_X86
    __m128i mask = _mm_loadu_si128((const __m128i*)table);
    return (i32x4) { .v = _mm_shuffle_epi8(a.v, mask) };
#elif CPU_ARM64
    uint8x16_t mask = vld1q_u8(table);
    int8x16_t result_s8 = vqtbl1q_s8(vreinterpretq_s8_s32(a.v), mask);
    return (i32x4) { .v = vreinterpretq_s32_s8(result_s8) };
#endif
}

// =============================================================================
// i16x8 Functions
// =============================================================================

static inline i16x8 i16x8_new(int16_t a, int16_t b, int16_t c, int16_t d, int16_t e, int16_t f, int16_t g, int16_t h) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_set_epi16(h, g, f, e, d, c, b, a) };
#elif CPU_ARM64
    int16_t temp[8] = { a, b, c, d, e, f, g, h };
    return (i16x8) { .v = vld1q_s16(temp) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_new_splat(int16_t a) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_set1_epi16(a) };
#elif CPU_ARM64
    return (i16x8) { .v = vdupq_n_s16(a) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_load_unaligned(const void* data, size_t offset) {
    uint8_t* ptr = (uint8_t*)(((i16*)data) + offset);
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_loadu_si128((const __m128i*)ptr) };
#elif CPU_ARM64
    return (i16x8) { .v = vld1q_s16((const int16_t*)ptr) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void i16x8_store_unaligned(i16x8 a, void* data, size_t offset) {
    uint8_t* ptr = (uint8_t*)(((i16*)data) + offset);
#if CPU_X64 || CPU_X86
    _mm_storeu_si128((__m128i*)ptr, a.v);
#elif CPU_ARM64
    vst1q_s16((int16_t*)ptr, a.v);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8x2 i16x8x2_load(const void* data, size_t offset) {
    uint8_t* ptr = (uint8_t*)(((i16*)data) + offset);
    i16x8x2 result;
#if CPU_X64 || CPU_X86
    result.v0.v = _mm_loadu_si128((const __m128i*)ptr);
    result.v1.v = _mm_loadu_si128((const __m128i*)(ptr + 16));
#elif CPU_ARM64
    result.v0.v = vld1q_s16((const int16_t*)ptr);
    result.v1.v = vld1q_s16((const int16_t*)(ptr + 16));
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_add(i16x8 a, i16x8 b) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_add_epi16(a.v, b.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vaddq_s16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_sub(i16x8 a, i16x8 b) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_sub_epi16(a.v, b.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vsubq_s16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_mul(i16x8 a, i16x8 b) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_mullo_epi16(a.v, b.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vmulq_s16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_lerp(i16x8 start, i16x8 end, i16x8 t) {
    i16x8 result;
#if CPU_X64 || CPU_X86
    result.v = _mm_add_epi16(_mm_mulhrs_epi16(_mm_sub_epi16(end.v, start.v), t.v), start.v);
#elif CPU_ARM64
    int16x8_t delta = vsubq_s16(end.v, start.v);
    // Use saturating add to prevent overflow wrapping to negative values.
    // This matches x86 behavior where the pack step treats overflowed values as large positives.
    result.v = vqaddq_s16(start.v, vqrdmulhq_s16(delta, t.v));
#endif
    return result;
}

#if CPU_X64 || CPU_X86
#define i16x8_extract(a, lane) (int16_t)_mm_extract_epi16((a).v, (lane))
#elif CPU_ARM64
#define i16x8_extract(a, lane) vgetq_lane_s16((a).v, (lane))
#endif

static inline i16x8 i16x8_mul_high(i16x8 a, i16x8 b) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_mulhrs_epi16(a.v, b.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vqrdmulhq_s16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_lerp_diff(i16x8 start, i16x8 delta, i16x8 t) {
    i16x8 result;
#if CPU_X64 || CPU_X86
    result.v = _mm_add_epi16(_mm_mulhrs_epi16(delta.v, t.v), start.v);
#elif CPU_ARM64
    // Use saturating add to prevent overflow wrapping to negative values.
    result.v = vqaddq_s16(start.v, vqrdmulhq_s16(delta.v, t.v));
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_table_shuffle(i16x8 a, const uint8_t table[16]) {
    i16x8 result;
#if CPU_X64 || CPU_X86
    __m128i mask = _mm_loadu_si128((const __m128i*)table);
    result.v = _mm_shuffle_epi8(a.v, mask);
#elif CPU_ARM64
    uint8x16_t mask = vld1q_u8(table);
    int8x16_t result_s8 = vqtbl1q_s8(vreinterpretq_s8_s16(a.v), mask);
    result.v = vreinterpretq_s16_s8(result_s8);
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_and(i16x8 a, i16x8 b) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_and_si128(a.v, b.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vandq_s16(a.v, b.v) };
#endif
}

#if CPU_X64 || CPU_X86
#define i16x8_shift_right(a, count) ((i16x8) { .v = _mm_srai_epi16((a).v, (count)) })
#elif CPU_ARM64
#define i16x8_shift_right(a, count) ((i16x8) { .v = vshrq_n_s16((a).v, (count)) })
#endif

static inline u8x16 i16x8_pack_high_to_u8x16(i16x8 p0, i16x8 p1) {
    u8x16 result;
#if CPU_X64 || CPU_X86
    __m128i p0_shifted = _mm_srli_epi16(p0.v, 8);
    __m128i p1_shifted = _mm_srli_epi16(p1.v, 8);
    result.v = _mm_packus_epi16(p0_shifted, p1_shifted);
#elif CPU_ARM64
    uint16x8_t p0_u16 = vreinterpretq_u16_s16(p0.v);
    uint16x8_t p1_u16 = vreinterpretq_u16_s16(p1.v);

    uint8x8_t high_bytes_p0 = vshrn_n_u16(p0_u16, 8);
    uint8x8_t high_bytes_p1 = vshrn_n_u16(p1_u16, 8);

    result.v = vcombine_u8(high_bytes_p0, high_bytes_p1);
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_rotate_4(i16x8 a) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_shuffle_epi32(a.v, _MM_SHUFFLE(1, 0, 3, 2)) };
#elif CPU_ARM64
    return (i16x8) { .v = vextq_s16(a.v, a.v, 4) };
#endif
}

#if CPU_X64 || CPU_X86
#define i16x8_splat_lane(a, lane) i16x8_new_splat(_mm_extract_epi16((a).v, (lane)))
#elif CPU_ARM64
#define i16x8_splat_lane(a, lane) ((i16x8) { .v = vdupq_laneq_s16((a).v, (lane)) })
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void i16x8_store_unaligned_ptr(i16x8 a, void* data) {
#if CPU_X64 || CPU_X86
    _mm_storeu_si128((__m128i*)data, a.v);
#elif CPU_ARM64
    vst1q_s16((int16_t*)data, a.v);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void i16x8_store_unaligned_ptr_lower(i16x8 a, void* data) {
#if CPU_X64 || CPU_X86
    _mm_storeu_si64((uint8_t*)data, a.v);
#elif CPU_ARM64
    vst1_s16((int16_t*)data, vget_low_s16(a.v));
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_load_unaligned_ptr(const void* data, size_t offset) {
    i16x8 result;
    const uint8_t* ptr = (const uint8_t*)data + offset;
#if CPU_X64 || CPU_X86
    result.v = _mm_loadu_si128((const __m128i*)ptr);
#elif CPU_ARM64
    result.v = vld1q_s16((const int16_t*)ptr);
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_cmp_lt(i16x8 a, i16x8 b) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_cmplt_epi16(a.v, b.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vreinterpretq_s16_u16(vcltq_s16(a.v, b.v)) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_write_mask(int32_t rest_count) {
    const int16_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    i16x8 index = i16x8_load_unaligned(data, 0);
    i16x8 cnt = i16x8_new_splat((int16_t)(rest_count & 0x7));
    return i16x8_cmp_lt(cnt, index);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 i16x8_select(i16x8 mask, i16x8 a, i16x8 b) {
    // Selects a where the mask is set, b where it is clear.
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = _mm_blendv_epi8(a.v, b.v, mask.v) };
#elif CPU_ARM64
    // vbslq takes its selected-when-set operand first, the reverse of _mm_blendv_epi8: a and b swap.
    return (i16x8) { .v = vreinterpretq_s16_u8(vbslq_u8(mask.v, b.v, a.v)) };
#endif
}

// =============================================================================
// u8x16 Functions
// =============================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_new_splat(uint8_t a) {
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_set1_epi8((int8_t)a) };
#elif CPU_ARM64
    return (u8x16) { .v = vdupq_n_u8(a) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_load_unaligned(const uint8_t* ptr, size_t offset) {
    const uint8_t* addr = ptr + offset;
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_loadu_si128((const __m128i*)addr) };
#elif CPU_ARM64
    return (u8x16) { .v = vld1q_u8(addr) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_new(const uint8_t data[16]) {
    return u8x16_load_unaligned(data, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void u8x16_store_unaligned(u8x16 a, uint8_t* ptr, size_t offset) {
    uint8_t* addr = ptr + offset;
#if CPU_X64 || CPU_X86
    _mm_storeu_si128((__m128i*)addr, a.v);
#elif CPU_ARM64
    vst1q_u8(addr, a.v);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_cmp_lt(u8x16 a, u8x16 b) {
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_cmplt_epi8(a.v, b.v) };
#elif CPU_ARM64
    // ARM64: Use vcltq_s8 (signed) to match _mm_cmplt_epi8 behavior
    int8x16_t a_signed = vreinterpretq_s8_u8(a.v);
    int8x16_t b_signed = vreinterpretq_s8_u8(b.v);
    uint8x16_t result = vreinterpretq_u8_s8(vcltq_s8(a_signed, b_signed));
    return (u8x16) { .v = result };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_and(u8x16 a, u8x16 b) {
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_and_si128(a.v, b.v) };
#elif CPU_ARM64
    return (u8x16) { .v = vandq_u8(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_or(u8x16 a, u8x16 b) {
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_or_si128(a.v, b.v) };
#elif CPU_ARM64
    return (u8x16) { .v = vorrq_u8(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_cmp_eq(u8x16 a, u8x16 b) {
#if CPU_X64 || CPU_X86
    return (u8x16) { .v = _mm_cmpeq_epi8(a.v, b.v) };
#elif CPU_ARM64
    return (u8x16) { .v = vceqq_u8(a.v, b.v) };
#endif
}

// Test if all lanes are true (0xFF)
static inline bool u8x16_all_true(u8x16 a) {
#if CPU_X64 || CPU_X86
    return _mm_movemask_epi8(a.v) == 0xFFFF;
#elif CPU_ARM64
    // ARM64: Use vminvq_u8 - if all lanes are 0xFF, minimum will be 0xFF
    return vminvq_u8(a.v) == 0xFF;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_table_shuffle(u8x16 a, const uint8_t table[16]) {
#if CPU_X64 || CPU_X86
    __m128i mask = _mm_loadu_si128((const __m128i*)table);
    return (u8x16) { .v = _mm_shuffle_epi8(a.v, mask) };
#elif CPU_ARM64
    uint8x16_t mask = vld1q_u8(table);
    int8x16_t result_s8 = vqtbl1q_s8(vreinterpretq_s8_u8(a.v), mask);
    return (u8x16) { .v = vreinterpretq_u8_s8(result_s8) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_shuffle2(u8x16 v0, u8x16 v1, u8x16 mask) {
    u8x16 result;
#if CPU_X64 || CPU_X86
    // Compare mask with 16: lt16[i] = -1 if mask[i] < 16, 0 otherwise
    __m128i lt16 = _mm_cmplt_epi8(mask.v, _mm_set1_epi8(16));

    // v0_mask: if mask[i] < 16, use mask[i]; else use -128 (outputs 0 in shuffle)
    __m128i v0_mask = _mm_blendv_epi8(_mm_set1_epi8(-128), mask.v, lt16);

    __m128i mask_minus_16 = _mm_sub_epi8(mask.v, _mm_set1_epi8(16));

    // Shuffle v0: select v0[mask[i]] when mask[i] < 16, else 0
    __m128i shuffled_v0 = _mm_shuffle_epi8(v0.v, v0_mask);

    // Shuffle v1: select v1[mask[i] - 16] when mask[i] >= 16, else 0
    __m128i shuffled_v1 = _mm_shuffle_epi8(v1.v, mask_minus_16);

    // Combine: since selections are exclusive, ORing gives the final result
    result.v = _mm_or_si128(shuffled_v0, shuffled_v1);
#elif CPU_ARM64
    uint8x16x2_t table = { { v0.v, v1.v } };
    result.v = vqtbl2q_u8(table, mask.v);
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u8x16 u8x16_write_mask(int32_t rest_count) {
    const uint8_t data[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    u8x16 index = u8x16_load_unaligned(data, 0);
    u8x16 cnt = u8x16_new_splat((uint8_t)(rest_count & 0xf));
    return u8x16_cmp_lt(cnt, index);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void u8x16_to_fixed_array(u8x16 a, uint8_t result[16]) {
    u8x16_store_unaligned(a, result, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8x2 u8x16_unpack_to_i16x8_high(u8x16 a) {
    i16x8x2 result;
#if CPU_X64 || CPU_X86
    __m128i zeros = _mm_setzero_si128();
    // Interleaving zeros ahead of each byte forms i16s of vN << 8 on little-endian x86.
    result.v0.v = _mm_unpacklo_epi8(zeros, a.v);
    result.v1.v = _mm_unpackhi_epi8(zeros, a.v);
#elif CPU_ARM64
    // vmovl_u8 zero-extends into the low byte, so shift up by 8 to match the x86 lane layout.
    uint16x8_t low_u16 = vmovl_u8(vget_low_u8(a.v));
    uint16x8_t high_u16 = vmovl_high_u8(a.v);
    result.v0.v = vreinterpretq_s16_u16(vshlq_n_u16(low_u16, 8));
    result.v1.v = vreinterpretq_s16_u16(vshlq_n_u16(high_u16, 8));
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8x2 u8x16_unpack_to_i16x8_low(u8x16 a) {
    i16x8x2 result;
#if CPU_X64 || CPU_X86
    __m128i zeros = _mm_setzero_si128();
    // Interleaving zeros after each byte forms i16s of vN on little-endian x86.
    result.v0.v = _mm_unpacklo_epi8(a.v, zeros);
    result.v1.v = _mm_unpackhi_epi8(a.v, zeros);
#elif CPU_ARM64
    // vmovl_u8 zero-extends each byte into the low half of its i16 lane, matching the x86 layout.
    result.v0.v = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(a.v)));
    result.v1.v = vreinterpretq_s16_u16(vmovl_high_u8(a.v));
#endif
    return result;
}

// Widen u8x16 to two i16x8 vectors with left shift
// Unpacks 16 u8 values into two i16x8 vectors, with each value shifted left by shift_count
// Input:  u8x16 with values [v0, v1, ..., v15]
// Output: i16x8_2.v0 = [v0<<shift, v1<<shift, ..., v7<<shift]
//         i16x8_2.v1 = [v8<<shift, v9<<shift, ..., v15<<shift]
//
// shift_count must be a compile-time constant (0-15)
#if CPU_X64 || CPU_X86
#define u8x16_unpack_to_i16x8_wide_shl(a, shift_count)                                                        \
    ((i16x8x2) { .v0 = { .v = _mm_slli_epi16(_mm_unpacklo_epi8((a).v, _mm_setzero_si128()), (shift_count)) }, \
                 .v1 = { .v = _mm_slli_epi16(_mm_unpackhi_epi8((a).v, _mm_setzero_si128()), (shift_count)) } })
#elif CPU_ARM64
#define u8x16_unpack_to_i16x8_wide_shl(a, shift_count)                                                \
    ((i16x8x2) { .v0 = { .v = vreinterpretq_s16_u16(vshll_n_u8(vget_low_u8((a).v), (shift_count))) }, \
                 .v1 = { .v = vreinterpretq_s16_u16(vshll_high_n_u8((a).v, (shift_count))) } })
#endif

// Widen u8x8 (8 bytes) to i16x8 with left shift
// Input:  8 bytes loaded from memory
// Output: i16x8 with values [v0<<shift, v1<<shift, ..., v7<<shift]
//
// shift_count must be a compile-time constant (0-15)
#if CPU_X64 || CPU_X86
#define u8x8_load_to_i16x8_wide_shl(ptr, offset, shift_count)                                                        \
    ((i16x8) {                                                                                                       \
        .v = _mm_slli_epi16(_mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)(ptr + offset)), _mm_setzero_si128()), \
                            (shift_count)) })
#elif CPU_ARM64
#define u8x8_load_to_i16x8_wide_shl(ptr, offset, shift_count) \
    ((i16x8) { .v = vreinterpretq_s16_u16(vshll_n_u8(vld1_u8(ptr + offset), (shift_count))) })
#endif

// Narrow two i16x8 vectors to u8x16 with right shift
// Packs 16 i16 values into a single u8x16 vector, with each value shifted right by shift_count
// Input:  i16x8_2.v0 = [v0, v1, ..., v7], i16x8_2.v1 = [v8, v9, ..., v15]
// Output: u8x16 with values [sat_u8(v0>>shift), sat_u8(v1>>shift), ..., sat_u8(v15>>shift)]
//
// The shift is logical; each shifted lane is then saturated to [0, 255].
// shift_count must be a compile-time constant (0-15 on x86, 1-16 on ARM64)
#if CPU_X64 || CPU_X86
#define i16x8x2_pack_to_u8x16_narrow_shr(input_v, shift_count)                      \
    ((u8x16) { .v = _mm_packus_epi16(_mm_srli_epi16((input_v).v0.v, (shift_count)), \
                                     _mm_srli_epi16((input_v).v1.v, (shift_count))) })
#elif CPU_ARM64
// The shift must be logical to match x86: an arithmetic vqshrun_n_s16 disagrees on negative lanes,
// where x86 _mm_srli_epi16(-128, 7) saturates to 255 and vqshrun_n_s16(-128, 7) saturates to 0.
#define i16x8x2_pack_to_u8x16_narrow_shr(input_v, shift_count)                                                 \
    ((u8x16) { .v = vcombine_u8(vqmovn_u16(vshrq_n_u16(vreinterpretq_u16_s16((input_v).v0.v), (shift_count))), \
                                vqmovn_u16(vshrq_n_u16(vreinterpretq_u16_s16((input_v).v1.v), (shift_count)))) })
#endif

// Unsigned unpacking function for premultiplication
static inline u16x8x2 u8x16_unpack_to_u16x8_low(u8x16 a) {
    u16x8x2 result;
#if CPU_X64 || CPU_X86
    __m128i zeros = _mm_setzero_si128();
    result.v0.v = _mm_unpacklo_epi8(a.v, zeros);
    result.v1.v = _mm_unpackhi_epi8(a.v, zeros);
#elif CPU_ARM64
    result.v0.v = vmovl_u8(vget_low_u8(a.v));
    result.v1.v = vmovl_u8(vget_high_u8(a.v));
#endif
    return result;
}

static inline u16x8x2 u16x8x2_mul(u16x8x2 a, u16x8x2 b) {
#if CPU_X64 || CPU_X86
    return (u16x8x2) {
        .v0 = (u16x8) { .v = _mm_mullo_epi16(a.v0.v, b.v0.v) },
        .v1 = (u16x8) { .v = _mm_mullo_epi16(a.v1.v, b.v1.v) },
    };
#elif CPU_ARM64
    return (u16x8x2) {
        .v0 = (u16x8) { .v = vmulq_u16(a.v0.v, b.v0.v) },
        .v1 = (u16x8) { .v = vmulq_u16(a.v1.v, b.v1.v) },
    };
#endif
}

static inline u16x8x2 u8x16_mul_widen(u8x16 a, u8x16 b) {
#if CPU_X64 || CPU_X86
    // x86: No direct widening multiply, unpack then multiply
    u16x8x2 a_wide = u8x16_unpack_to_u16x8_low(a);
    u16x8x2 b_wide = u8x16_unpack_to_u16x8_low(b);
    return u16x8x2_mul(a_wide, b_wide);
#elif CPU_ARM64
    // ARM: Direct widening multiply - 2 instructions total
    return (u16x8x2) { .v0 = (u16x8) { .v = vmull_u8(vget_low_u8(a.v), vget_low_u8(b.v)) },
                       .v1 = (u16x8) { .v = vmull_high_u8(a.v, b.v) } };
#endif
}

typedef struct {
    u8x16 r;
    u8x16 g;
    u8x16 b;
} u8x16_rgb_result;

static inline u8x16_rgb_result u8x16_load_deinterleaved_3(const uint8_t* data, size_t offset) {
    u8x16_rgb_result result;
    const uint8_t* ptr = data + offset;

#if CPU_ARM64
    uint8x16x3_t loaded = vld3q_u8(ptr);
    result.r.v = loaded.val[0];
    result.g.v = loaded.val[1];
    result.b.v = loaded.val[2];
#elif CPU_X64 || CPU_X86
    // x86 fallback: scalar deinterleave
    uint8_t temp_r[16], temp_g[16], temp_b[16];
    for (int i = 0; i < 16; i++) {
        temp_r[i] = ptr[i * 3 + 0];
        temp_g[i] = ptr[i * 3 + 1];
        temp_b[i] = ptr[i * 3 + 2];
    }
    result.r = u8x16_new(temp_r);
    result.g = u8x16_new(temp_g);
    result.b = u8x16_new(temp_b);
#endif
    return result;
}

typedef struct {
    u8x16 r;
    u8x16 g;
    u8x16 b;
    u8x16 a;
} u8x16_rgba_result;

static inline u8x16_rgba_result u8x16_load_deinterleaved_4(const uint8_t* data, size_t offset) {
    u8x16_rgba_result result;
    const uint8_t* ptr = data + offset;

#if CPU_ARM64
    uint8x16x4_t loaded = vld4q_u8(ptr);
    result.r.v = loaded.val[0];
    result.g.v = loaded.val[1];
    result.b.v = loaded.val[2];
    result.a.v = loaded.val[3];
#elif CPU_X64 || CPU_X86
    // x86 fallback: scalar deinterleave
    uint8_t temp_r[16], temp_g[16], temp_b[16], temp_a[16];
    for (int i = 0; i < 16; i++) {
        temp_r[i] = ptr[i * 4 + 0];
        temp_g[i] = ptr[i * 4 + 1];
        temp_b[i] = ptr[i * 4 + 2];
        temp_a[i] = ptr[i * 4 + 3];
    }
    result.r = u8x16_new(temp_r);
    result.g = u8x16_new(temp_g);
    result.b = u8x16_new(temp_b);
    result.a = u8x16_new(temp_a);
#endif
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void u8x16_store_interleaved_3(uint8_t* data, u8x16 r, u8x16 g, u8x16 b, size_t offset) {
    uint8_t* ptr = data + offset;

#if CPU_ARM64
    uint8x16x3_t to_store = { { r.v, g.v, b.v } };
    vst3q_u8(ptr, to_store);
#elif CPU_X64 || CPU_X86
    // x86 fallback: scalar interleave
    uint8_t temp_r[16], temp_g[16], temp_b[16];
    u8x16_store_unaligned(r, temp_r, 0);
    u8x16_store_unaligned(g, temp_g, 0);
    u8x16_store_unaligned(b, temp_b, 0);

    for (int i = 0; i < 16; i++) {
        ptr[i * 3 + 0] = temp_r[i];
        ptr[i * 3 + 1] = temp_g[i];
        ptr[i * 3 + 2] = temp_b[i];
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void u8x16_store_interleaved_4(uint8_t* data, u8x16 r, u8x16 g, u8x16 b, u8x16 a, size_t offset) {
    uint8_t* ptr = data + offset;

#if CPU_ARM64
    uint8x16x4_t to_store = { { r.v, g.v, b.v, a.v } };
    vst4q_u8(ptr, to_store);
#elif CPU_X64 || CPU_X86
    // Simple fallback for x64
    uint8_t temp_r[16], temp_g[16], temp_b[16], temp_a[16];
    u8x16_store_unaligned(r, temp_r, 0);
    u8x16_store_unaligned(g, temp_g, 0);
    u8x16_store_unaligned(b, temp_b, 0);
    u8x16_store_unaligned(a, temp_a, 0);

    for (int i = 0; i < 16; i++) {
        ptr[i * 4 + 0] = temp_r[i];
        ptr[i * 4 + 1] = temp_g[i];
        ptr[i * 4 + 2] = temp_b[i];
        ptr[i * 4 + 3] = temp_a[i];
    }
#endif
}

// =============================================================================
// Cross-type conversions
// =============================================================================

static inline i32x4 f32x4_as_i32x4(f32x4 a) {
#if CPU_X64 || CPU_X86
    return (i32x4) { .v = _mm_cvttps_epi32(a.v) };
#elif CPU_ARM64
    return (i32x4) { .v = vcvtq_s32_f32(a.v) };
#endif
}

// =============================================================================
// C11 Generic Macros
// =============================================================================

#if __STDC_VERSION__ >= 201112L
#define simd_add(a, b) _Generic((a), f32x4: f32x4_add, i32x4: i32x4_add, i16x8: i16x8_add)(a, b)

#define simd_new_splat(val)       \
    _Generic((val),               \
        float: f32x4_new_splat,   \
        int32_t: i32x4_new_splat, \
        int16_t: i16x8_new_splat, \
        uint8_t: u8x16_new_splat)(val)

// =============================================================================
// u16x8 Functions
// =============================================================================

static inline i16x8 u16x8_as_i16x8(u16x8 a) {
#if CPU_X64 || CPU_X86
    return (i16x8) { .v = a.v };
#elif CPU_ARM64
    return (i16x8) { .v = vreinterpretq_s16_u16(a.v) };
#endif
}

// =============================================================================
// f16x8 Functions (8 half-precision floats)
// =============================================================================

#if CPU_X64 || CPU_X86
// On x64, two f32x4 values are held internally
typedef struct {
    f32x4 low;  // Elements 0-3
    f32x4 high; // Elements 4-7
} f16x8;
#elif CPU_ARM64
typedef struct {
    float16x8_t v;
} f16x8;
#endif

static inline f16x8 f16x8_new_splat(float a) {
#if CPU_X64 || CPU_X86
    return (f16x8) { .low = f32x4_new_splat(a), .high = f32x4_new_splat(a) };
#elif CPU_ARM64
    return (f16x8) { .v = vdupq_n_f16((__fp16)a) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f16x8 f16x8_new(float a, float b, float c, float d, float e, float f, float g, float h) {
#if CPU_X64 || CPU_X86
    return (f16x8) { .low = f32x4_new(a, b, c, d), .high = f32x4_new(e, f, g, h) };
#elif CPU_ARM64
    __fp16 temp[8] = { (__fp16)a, (__fp16)b, (__fp16)c, (__fp16)d, (__fp16)e, (__fp16)f, (__fp16)g, (__fp16)h };
    return (f16x8) { .v = vld1q_f16(temp) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f16x8 f16x8_add(f16x8 a, f16x8 b) {
#if CPU_X64 || CPU_X86
    return (f16x8) { .low = f32x4_add(a.low, b.low), .high = f32x4_add(a.high, b.high) };
#elif CPU_ARM64
    return (f16x8) { .v = vaddq_f16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f16x8 f16x8_sub(f16x8 a, f16x8 b) {
#if CPU_X64 || CPU_X86
    return (f16x8) { .low = f32x4_sub(a.low, b.low), .high = f32x4_sub(a.high, b.high) };
#elif CPU_ARM64
    return (f16x8) { .v = vsubq_f16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f16x8 f16x8_mul(f16x8 a, f16x8 b) {
#if CPU_X64 || CPU_X86
    return (f16x8) { .low = f32x4_mul(a.low, b.low), .high = f32x4_mul(a.high, b.high) };
#elif CPU_ARM64
    return (f16x8) { .v = vmulq_f16(a.v, b.v) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Approximate square root: a * rsqrt_estimate(a). x86 uses the bare _mm_rsqrt_ps estimate; ARM64 adds one
// Newton-Raphson step. An input of 0 yields NaN on x86.
static inline f16x8 f16x8_sqrt_approx(f16x8 a) {
#if CPU_X64 || CPU_X86
    // x86 doesn't have native f16 rsqrt, so use f32 path with conversion
    __m128 rsqrt_low = _mm_rsqrt_ps(a.low.v);
    __m128 rsqrt_high = _mm_rsqrt_ps(a.high.v);
    f32x4 sqrt_low = f32x4_mul(a.low, (f32x4) { .v = rsqrt_low });
    f32x4 sqrt_high = f32x4_mul(a.high, (f32x4) { .v = rsqrt_high });
    return (f16x8) { .low = sqrt_low, .high = sqrt_high };
#elif CPU_ARM64
    float16x8_t rsqrt_est = vrsqrteq_f16(a.v);
    float16x8_t rsqrt_refined = vmulq_f16(rsqrt_est, vrsqrtsq_f16(vmulq_f16(a.v, rsqrt_est), rsqrt_est));
    return (f16x8) { .v = vmulq_f16(a.v, rsqrt_refined) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline f16x8 f16x8_clamp(f16x8 a, f16x8 min, f16x8 max) {
#if CPU_X64 || CPU_X86
    return (f16x8) { .low = f32x4_clamp(a.low, min.low, max.low), .high = f32x4_clamp(a.high, min.high, max.high) };
#elif CPU_ARM64
    return (f16x8) { .v = vmaxq_f16(min.v, vminq_f16(a.v, max.v)) };
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline i16x8 f16x8_as_i16x8(f16x8 a) {
#if CPU_X64 || CPU_X86
    i32x4 low_i32 = f32x4_as_i32x4(a.low);
    i32x4 high_i32 = f32x4_as_i32x4(a.high);
    return (i16x8) { .v = _mm_packs_epi32(low_i32.v, high_i32.v) };
#elif CPU_ARM64
    return (i16x8) { .v = vcvtq_s16_f16(a.v) };
#endif
}

static inline f32x4 f16x8_extract_low_as_f32x4(f16x8 a) {
#if CPU_X64 || CPU_X86
    return a.low;
#elif CPU_ARM64
    float16x4_t low_half = vget_low_f16(a.v);
    return (f32x4) { .v = vcvt_f32_f16(low_half) };
#endif
}

static inline f32x4 f16x8_extract_high_as_f32x4(f16x8 a) {
#if CPU_X64 || CPU_X86
    return a.high;
#elif CPU_ARM64
    float16x4_t high_half = vget_high_f16(a.v);
    return (f32x4) { .v = vcvt_f32_f16(high_half) };
#endif
}

#if CPU_X64 || CPU_X86
#define simd_extract(vec, lane)                                                                              \
    _Generic((vec),                                                                                          \
        f32x4: _mm_cvtss_f32(_mm_shuffle_ps((vec).v, (vec).v, _MM_SHUFFLE((lane), (lane), (lane), (lane)))), \
        i32x4: _mm_extract_epi32((vec).v, (lane)),                                                           \
        i16x8: (int16_t)_mm_extract_epi16((vec).v, (lane)))
#elif CPU_ARM64
#define simd_extract(vec, lane)                 \
    _Generic((vec),                             \
        f32x4: vgetq_lane_f32((vec).v, (lane)), \
        i32x4: vgetq_lane_s32((vec).v, (lane)), \
        i16x8: vgetq_lane_s16((vec).v, (lane)))
#endif

#endif // __STDC_VERSION__ >= 201112L

#ifdef __cplusplus
}
#endif

static inline i16x8x2 i16x8x2_mul(i16x8x2 a, i16x8x2 b) {
    return (i16x8x2) {
        .v0 = i16x8_mul(a.v0, b.v0),
        .v1 = i16x8_mul(a.v1, b.v1),
    };
}
