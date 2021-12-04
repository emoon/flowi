#pragma once

#include "flowi.h"
#include "../include/config.h"
#include "../include/error.h"

#if defined(FL_FONTLIB_FREETYPE)
#include <freetype/freetype.h>
#endif

#define FL_FONTS_MAX 32

struct Font;

// Used to build up the render state
typedef struct BuildRenderState {
    u8* render_data;
    u8* render_commands;
    u8* start_render_data;
    u8* start_render_commands;
    u8* end_render_data;
    u8* end_render_commands;
} BuildRenderState;

typedef struct FlGlobalState {
#if defined(FL_FONTLIB_FREETYPE)
	FT_Library ft_library;
#endif
    BuildRenderState render_data;
    // TODO: Fix max number of fonts
    struct Font* fonts[FL_FONTS_MAX];
    int font_count;
    u32 temp;
    u16 texture_ids;
} FlGlobalState;

#if defined(__GNUC__) || defined(__clang__)
#include <stdalign.h>
#define FL_LIKELY(x) __builtin_expect((x),1)
#define FL_UNLIKELY(x) __builtin_expect((x),0)
#define FL_ALIGNOF(_type) alignof(_type)
#else
#define FL_ALIGNOF(_type) __alignof(_type)
#define FL_LIKELY(x) (x)
#define FL_UNLIKELY(x) (x)
#endif

#define FL_MIN(a, b) ((a) < (b)) ? (a) : (b)
#define FL_MAX(a, b) ((a) > (b)) ? (a) : (b)

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


