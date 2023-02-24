#pragma once

#include <flowi/inline.h>
#include <flowi/math_data.h>
#include <stdbool.h>
#include <stdint.h>

#define FL_RESTRICT __restrict

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef float f32;
typedef double f64;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef FLT_MAX
#define FLT_MAX 3.402823466e+38F
#endif

#if defined(__GNUC__) || defined(__clang__)
#include <stdalign.h>
#define FL_LIKELY(x) __builtin_expect((x), 1)
#define FL_UNLIKELY(x) __builtin_expect((x), 0)
#define FL_ALIGNOF(_type) alignof(_type)
#else
#define FL_ALIGNOF(_type) __alignof(_type)
#define FL_LIKELY(x) (x)
#define FL_UNLIKELY(x) (x)
#endif

#define FL_MIN(a, b) ((a) < (b)) ? (a) : (b)
#define FL_MAX(a, b) ((a) > (b)) ? (a) : (b)
#define FL_UNUSED(a) (void)a
#define FL_SIZEOF_ARRAY(x) sizeof(x) / sizeof(x[0])

#if defined(__GNUC__)
#if defined(__clang__)
#define FL_ASSUME(cond) __builtin_assume(cond)
#else
#define FL_ASSUME(cond)              \
    do {                             \
        if (!(cond))                 \
            __builtin_unreachable(); \
    } while (0)
#endif
#else
#define FL_ASSUME(cond) \
    while (0) {         \
    }
#endif
