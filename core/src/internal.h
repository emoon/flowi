#pragma once

#include "flowi.h"
#include "../include/config.h"
#include "../include/error.h"

#if defined(FL_FONTLIB_FREETYPE)
#include <freetype/freetype.h>
#endif

typedef struct FlGlobalState {
#if defined(FL_FONTLIB_FREETYPE)
	FT_Library ft_library;
#endif
    u32 temp;
} FlGlobalState;

#if defined(__GNUC__) || defined(__clang__)
#define FL_LIKELY(x) __builtin_expect((x),1)
#define FL_UNLIKELY(x) __builtin_expect((x),0)
#define FL_ALIGNOF(_type) alignof(_type)
#else
#define FL_ALIGNOF(_type) __alignof(_type)
#define FL_LIKELY(x) (x)
#define FL_UNLIKELY(x) (x)
#endif

// Global state for the whole lib
// Contains loaded fonts, etc
extern FlGlobalState* g_state;

// These are for internal library wise functions. This header should never
// be included in the public headers!

#define ERROR_ADD(t, format, ...) \
    Errors_add(t, __FILE__, __LINE__, format, __VA_ARGS__);

void Errors_add(FlError err, const char* filename, int line, const char* fmt, ...);

void Font_init(FlGlobalState* ctx);

// TODO: Use custom io functions
// TODO: Custom allocator
u8* Io_load_file_to_memory(const char* filename, u32* out_size);
