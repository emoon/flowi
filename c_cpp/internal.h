#pragma once

#include <flowi/image.h>
#include <flowi/button.h>
#include <flowi/error.h>
#include <flowi/font.h>
#include <flowi/image.h>
#include <flowi/io.h>
#include <flowi/item.h>
#include <flowi/layout.h>
#include <flowi/menu.h>
#include <flowi/style.h>
#include <flowi/text.h>
#include <flowi/window.h>
//#include "../external/hashmap.h"
#include "command_buffer.h"
#include "string_allocator.h"
#include "layer.h"
#include "flowi_internal.h"
#include "handles.h"
#include "allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_MAX_WIDGET_IDS 2048

// TODO: Move
#define FL_RGB(r, g, b) (((u32)b) << 16 | (((u32)g) << 8) | ((u32)r))
#define FL_RGB_RED FL_RGB(255, 0, 0)
#define FL_RGB_WHITE FL_RGB(255, 255, 255)
#define FL_RGB_BLACK FL_RGB(0, 0, 0)

struct StyleInternal;
struct Atlas;
struct ImGuiContext;
struct ImFontAtlas;

#define FlLayerType_Count 1

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlGlobalState {
    struct FlAllocator* global_allocator;

    // Handles
    Handles image_handles;

    CommandBuffer render_commands;

    struct Atlas* images_atlas;
    struct ImFontAtlas* font_atlas;

    u16 texture_ids;
} FlGlobalState;

// These are for internal library wise functions. This header should never
// be included in the public headers!

#define ERROR_ADD(t, format, ...) Errors_add(t, __FILE__, __LINE__, format, __VA_ARGS__);

void Errors_add(FlError err, const char* filename, int line, const char* fmt, ...);

typedef struct FlInternalData {
    struct ImGuiContext* imgui_ctx;
    struct FlButtonApi button_funcs;
    struct FlCursorApi cursor_funcs;
    struct FlFontApi font_funcs;
    struct FlImageApi image_funcs;
    struct FlIoApi io_funcs;
    struct FlItemApi item_funcs;
    struct FlMenuApi menu_funcs;
    struct FlStyleApi style_funcs;
    struct FlTextApi text_funcs;
    struct FlUiApi ui_funcs;
    struct FlWindowApi window_funcs;
    LinearAllocator frame_allocator;
    StringAllocator string_allocator;
    Layer layers[FlLayerType_Count];
    struct FlGlobalState* global;
    void* io_handler;
    //struct hashmap_s widget_states;
    float delta_time;
} FlInternalData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Use custom io functions
// TODO: Custom allocator

u8* Io_load_file_to_memory_null_term(FlInternalData* ctx, const char* filename, u32* out_size);
u8* Io_load_file_to_memory(FlInternalData* ctx, const char* filename, u32* out_size);
u8* Io_load_file_to_memory_flstring(FlInternalData* ctx, FlString name, u32* out_size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Move

/*
FL_INLINE FlVec2 vec2_sub(FlVec2 a, FlVec2 b) {
    return (FlVec2){a.x - b.x, a.y - b.y};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE FlVec2 vec2_zero() {
    return (FlVec2){0.0f, 0.0f};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE float vec2_length_sqr(FlVec2 v) {
    return (v.x * v.x) + (v.y * v.y);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE f32 f32_abs(f32 v) {
    return v < 0.0f ? -v : v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE FlVec2 vec2_floor(FlVec2 v) {
    return (FlVec2){(float)(int)v.x, (float)(int)v.y};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE f32 f32_max(f32 v0, f32 v1) {
    return v0 > v1 ? v0 : v1;
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

