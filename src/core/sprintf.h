#pragma once

#include "string.h"
#include <stdarg.h>

struct FlArena;
struct FlArenaMt;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sprintf with SIMD vector support
//
// Every standard format specifier works, plus one specifier per simd.h vector type. Components print
// in bracket notation - floats with %.6g, integers with %d or %u - and an unrecognized vector
// specifier is emitted as literal text with the % dropped.
//
//   %v4f     f32x4     %v8i16   i16x8     %v16u8   u8x16
//   %v4i     i32x4     %v8u16   u16x8
//   %v4u     u32x4     %v8h     f16x8
//
//   f32x4 pos = f32x4_new(10.5f, 20.25f, -5.0f, 1.0f);
//   fl_sprintf_arena(arena, "Position: %v4f", pos);   // "Position: [10.5, 20.25, -5, 1]"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The result is one contiguous run of bytes in arena, whatever else is allocating from it: the
// message is assembled in a thread-local scratch arena and the arena is bumped once. A caller
// formatting into a scratch scope of its own may pass that scratch arena - assembly moves to the
// other one. Both entry points take the calling thread's scratch arenas, so neither may be called
// from a signal handler or from anything reached by one.
//
// fl_sprintf_arena is declared in <flowi/string/string.h>.
FlString vsprintf_arena(struct FlArena* arena, const char* format, va_list args);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sprintf onto an FlArenaMt
//
// Same contract as fl_sprintf_arena/vsprintf_arena, for the arenas held as FlArenaMt rather than
// FlArena.
//
// Example:
//   FlArenaMt* arena_mt = arena_mt_new();
//   FlString msg = vsprintf_arena_mt(arena_mt, "Error: %S", error_path);

FlString sprintf_arena_mt(struct FlArenaMt* arena_mt, const char* format, ...);
FlString vsprintf_arena_mt(struct FlArenaMt* arena_mt, const char* format, va_list args);
