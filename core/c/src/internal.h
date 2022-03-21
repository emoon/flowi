#pragma once

#include <flowi_core/config.h>
#include <flowi_core/error.h>
#include <flowi_core/layout.h>
#include "command_buffer.h"
#include "flowi.h"
#include "handles.h"
#include "layout_private.h"
#include "primitives.h"
#include "simd.h"
#include "string_allocator.h"
#include "vertex_allocator.h"

#if defined(FL_FONTLIB_FREETYPE)
#include <freetype/freetype.h>
#endif

#define FL_FONTS_MAX 32
#define FL_MAX_STYLES 128
#define FL_MAX_LAYOUTS 256
#define FL_STYLE_DEPTH 128

// TODO: Move
#define FL_RGB(r, g, b) (((u32)b) << 16 | (((u32)g) << 8) | ((u32)r))
#define FL_RGB_RED FL_RGB(255, 0, 0)
#define FL_RGB_WHITE FL_RGB(255, 255, 255)
#define FL_RGB_BLACK FL_RGB(0, 0, 0)

struct Font;
struct StyleInternal;
struct Atlas;

typedef u32 FlowiID;

typedef struct FlGlobalState {
#if defined(FL_FONTLIB_FREETYPE)
    FT_Library ft_library;
#endif
    FlAllocator* global_allocator;

    // Handles
    Handles image_handles;
    Handles font_handles;

    // Primitive commands
    CommandBuffer primitive_commands;

    // Render commands that is generated for the rendering backend
    CommandBuffer render_commands;

    // TODO: We may have to support more atlases, but right now we have three
    // One for grayscale fonts, one for colored fonts and one for images.
    struct Atlas* mono_fonts_atlas;
    struct Atlas* color_fonts_atlas;
    struct Atlas* images_atlas;

    u16 texture_ids;
} FlGlobalState;

// These are for internal library wise functions. This header should never
// be included in the public headers!

#define ERROR_ADD(t, format, ...) Errors_add(t, __FILE__, __LINE__, format, __VA_ARGS__);

void Errors_add(FlError err, const char* filename, int line, const char* fmt, ...);

void Font_init(FlGlobalState* ctx);

typedef struct Rect {
    float x, y, width, height;
} Rect;

typedef struct IntRect {
    int x, y, width, height;
} IntRect;

typedef struct IntAABB {
    int min_x, min_y, max_x, max_y;
} IntAABB;

// TODO: We can lilkey do this better
typedef struct ItemWithText {
    char text[1024];
    int len;
} ItemWithText;

#define MAX_MOUSE_BUTTONS 3

typedef struct MouseState {
    // = 0.30f - Time for a double-click, in seconds.
    f32 double_click_time;
    // = 6.0f - Distance threshold to stay in to validate a double-click, in pixels.
    f32 double_click_max_dist;
    // Time of last click (used to figure out double-click)
    f64 clicked_time[MAX_MOUSE_BUTTONS];
    // Current mouse position
    FlVec2 pos;
    // Previous mouse position (note that Delta is not necessary == Pos-PosPrev, in case either position is invalid)
    FlVec2 pos_prev;
    // Delta direction of the mouse
    FlVec2 delta;
    // Position at time of clicking
    FlVec2 clicked_pos[MAX_MOUSE_BUTTONS];
    // Maximum distance, absolute, on each axis, of how much mouse has traveled from the clicking point
    FlVec2 drag_max_distance_abs[MAX_MOUSE_BUTTONS];
    // Duration the mouse button has been down (0.0f == just clicked)
    f32 down_duration[MAX_MOUSE_BUTTONS];
    // Previous time the mouse button has been down
    f32 down_duration_prev[MAX_MOUSE_BUTTONS];
    // Squared maximum distance of how much mouse has traveled from the clicking point
    f32 drag_max_distance_sqr[MAX_MOUSE_BUTTONS];
    // Updated by external code
    bool down[MAX_MOUSE_BUTTONS];
    // Mouse button went from !Down to Down
    bool clicked[MAX_MOUSE_BUTTONS];
    // Has mouse button been double-clicked?
    bool double_clicked[MAX_MOUSE_BUTTONS];
    // Mouse button went from Down to !Down
    bool released[MAX_MOUSE_BUTTONS];
    // Track if button down was a double-click
    bool down_was_double_click[MAX_MOUSE_BUTTONS];
    // TODO: Move?
    bool nav_disable_hover;
} MouseState;

typedef struct FlContext {
    // Current time
    double time;
    // Current delta time (usually 1.0f/60 at 60 fps update)
    float delta_time;
    // Tracks the mouse state for this frame
    MouseState mouse;
    // hash of the full context. Use for to skip rendering if nothing has changed
    // XXH3_state_t context_hash;
    // Previous frames hash. We can check against this to see if anything has changed
    // XXH3_state_t prev_frame_hash;
    FlVec2 cursor;
    // id from the previous frame
    u32 prev_active_item;
    // current id
    u32 active_item;
    // TODO: Likely need block allocator here instead
    vec128* positions;
    // TODO: Likely need block allocator here instead
    vec128* colors;
    // TODO: Likely need block allocator here instead
    u32* widget_ids;

    struct Font* current_font;
    // count of all widgets
    int widget_count;
    // Times with text (push button, labels, etc)
    int items_with_text_count;
    ItemWithText* items_with_text;
    // Active fade actions
    int fade_actions;

    // Used for tracking active widgets
    FlowiID hovered_id;
    FlowiID active_id;

    FlGlobalState* global;

    // Used for building vertex / index output
    VertexAllocator vertex_allocator;
    LinearAllocator layout_allocator;
    LayoutAreaPrivate* current_layout;
    FlLayoutAreaId default_layout;
    FlLayoutAreaId active_layout;
    FlLayoutMode layout_mode;
    LinearAllocator frame_allocator;

    StringAllocator string_allocator;

    struct StyleInternal* styles[FL_MAX_STYLES];        // TODO: Dynamic array instead of hard-coded max style
    struct StyleInternal* style_stack[FL_STYLE_DEPTH];  // Having 128 max styles should be enough

    int current_font_size;
    int frame_count;
    int style_count;
    int layout_count;
    int style_stack_depth;

    int layout_ids;

} FlContext;

enum ButtonFlags {
    // return true on click (mouse down event)
    ButtonFlags_PressedOnClick = 1 << 4,
    // [Default] return true on click + release on same item <-- this is what the majority of Button are using
    ButtonFlags_PressedOnClickRelease = 1 << 5,
    // return true on click + release even if the release event is not done while hovering the item
    ButtonFlags_PressedOnClickReleaseAnywhere = 1 << 6,
    // return true on release (default requires click+release)
    ButtonFlags_PressedOnRelease = 1 << 7,
    // return true on double-click (default requires click+release)
    ButtonFlags_PressedOnDoubleClick = 1 << 8,
    // return true when held into while we are drag and dropping another item (used by e.g. tree nodes, collapsing
    // headers)
    ButtonFlags_PressedOnDragDropHold = 1 << 9,
};

// TODO: Use custom io functions
// TODO: Custom allocator
u8* Io_load_file_to_memory_null_term(FlContext* ctx, const char* filename, u32* out_size);
u8* Io_load_file_to_memory(FlContext* ctx, const char* filename, u32* out_size);
u8* Io_load_file_to_memory_flstring(FlContext* ctx, FlString name, u32* out_size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Move

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
