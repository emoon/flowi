#pragma once

#include <stdint.h>

#define FL_RESTRICT __restrict

#if defined(__GNUC__)
#define FL_INLINE static inline
#elif defined(_MSC_VER)
#define FL_INLINE __forceinline static
#else
#error "Unsupported compiler"
#endif

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

