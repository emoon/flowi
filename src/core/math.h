#pragma once

#include "simd.h"
#include "types.h"

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

typedef struct Ivec2 {
    int x;
    int y;
} Ivec2;

static inline u64 align_pow2(u64 value, u64 alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Bit manipulation functions

static inline u32 clz_u64(u64 value) {
#if COMPILER_CLANG || COMPILER_GCC
    return value ? __builtin_clzll(value) : 64;
#else
#error "Unsupported compiler"
#endif
}

static inline int count_trailing_zeros(unsigned int value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(value);
#else
#error "Unsupported compiler"
#endif
}

static inline f32 abs_f32(f32 v) {
    return v < 0.0f ? -v : v;
}

static inline f32 lerp(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

static inline f32 inv_lerp(f32 a, f32 b, f32 v) {
    return (v - a) / (b - a);
}
