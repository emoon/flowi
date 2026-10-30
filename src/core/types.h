#pragma once

#include "core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

typedef uintptr_t usize_ptr;
typedef intptr_t isize_ptr;

#ifndef __cplusplus
// Define nullptr for C11 compatibility (nullptr is a keyword in C23+).
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define nullptr ((void*)0)
#endif
#endif
