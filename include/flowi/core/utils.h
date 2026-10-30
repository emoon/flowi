#pragma once

// Bare-name convenience macros for implementation code. The spellings (min, max, clamp,
// for_count) are unprefixed and would collide in a consumer's namespace, so this header is
// explicitly opt-in and must never be included by another public flowi header.

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)

// Strips the const off a type so a counter derived from a const bound stays assignable.
#define REMOVE_CONST(T) __typeof__((T) + 0)

// Iterates i over [0, n), evaluating n exactly once.
#define for_count(i, n) for (REMOVE_CONST(n) i = 0, CONCAT(_count_, __LINE__) = (n); i < CONCAT(_count_, __LINE__); ++i)

#define sizeof_array(arr) (sizeof(arr) / sizeof((arr)[0]))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Math utility macros
//
// These evaluate their arguments more than once, so pass side-effect-free expressions. Each keeps the
// argument's own type.
//
// A function-like macro of the same name breaks <algorithm>'s std::min/std::max/std::clamp declarations,
// so C++ consumers get the standard ones instead.

#ifndef __cplusplus

// Windows headers define min/max as macros; ours win.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

#endif
