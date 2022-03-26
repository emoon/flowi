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

// Used to determine what kind of navigation we are doing
typedef enum InputSource {
    InputSource_None = 0,
    InputSource_Mouse,
    InputSource_Nav,
    // Only used occasionally for storage, not tested/handled by most code
    InputSource_NavKeyboard,
    // Only used occasionally for storage, not tested/handled by most code
    InputSource_NavGamepad,
    ImGuiInputSource_COUNT
} InputSource;

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
    // Temporary text input when CTRL+clicking on a slider, etc.
    FlowiID temp_input_id;
    // Hovered widget, filled during the frame
    FlowiID hovered_id;
    FlowiID hovered_id_previous_frame;
    bool hovered_id_allow_overlap;
    // Hovered widget will use mouse wheel. Blocks scrolling the underlying window.
    bool hovered_id_using_mouse_wheel;
    bool hovered_id_previous_frame_using_mouse_wheel;
    // At least one widget passed the rect test, but has been discarded by disabled flag or popup inhibit. May be true
    // even if HoveredId == 0.
    bool hovered_id_disabled;
    // Measure contiguous hovering time
    float hovered_id_timer;
    // Measure contiguous hovering time where the item has not been active
    float hovered_id_not_active_timer;
    // Data for tracking the active id
    // Active widget
    FlowiID active_id;
    // Active widget has been seen this frame (we can't use a bool as the active_id may change within the frame)
    FlowiID active_id_is_alive;
    float active_id_timer;
    // Store the last non-zero active_id, useful for animation.
    FlowiID last_active_id;
    // Store the last non-zero active_id timer since the beginning of activation, useful for animation.
    float last_active_id_timer;
    // Active widget will want to read those nav move requests (e.g. can activate a button and move away from it)
    u32 active_id_using_nav_dir_mask;
    // Active widget will want to read those nav inputs.
    u32 active_id_using_nav_input_mask;
    // Active widget will want to read those key inputs. When we grow the ImGuiKey enum we'll need to either to order
    // the enum to make useful keys come first, either redesign this into e.g. a small array.
    u32 active_id_using_key_input_mask;
    // Clicked offset from upper-left corner, if applicable (currently only set by ButtonBehavior)
    FlVec2 active_id_click_offset;
    // Activating with mouse or nav (gamepad/keyboard) //InputSource Source;
    int active_id_mouse_button;
    FlowiID active_id_previous_frame;
    // Store the last non-zero active_id, useful for animation.
    FlowiID active_id_last;
    // Store the last non-zero active_id timer since the beginning of activation, useful for animation.
    float active_id_last_Timer;
    // Set at the time of activation for one frame
    bool active_id_is_just_activated;
    // Active widget allows another widget to steal active id (generally for overlapping widgets, but not always)
    bool active_id_allow_overlap;
    // Disable losing active id if the active id window gets unfocused.
    bool active_id_no_clear_on_focus_loss;
    // Track whether the active id led to a press (this is to allow changing between PressOnClick and PressOnRelease
    // without pressing twice). Used by range_select branch.
    bool active_id_has_been_pressed_before;
    // Was the value associated to the widget Edited over the course of the Active state.
    bool active_id_has_been_edited_before;
    bool active_id_has_been_edited_this_frame;
    // Active widget will want to read mouse wheel. Blocks scrolling the underlying window.
    bool active_id_using_mouse_wheel;
    bool active_id_previous_frame_is_alive;
    bool active_id_previous_frame_has_been_edited_before;
    // ActiveId active_id;
    //  hash of the full context. Use for to skip rendering if nothing has changed
    //  XXH3_state_t context_hash;
    //  Previous frames hash. We can check against this to see if anything has changed
    //  XXH3_state_t prev_frame_hash;
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
    // React on left mouse button (default)
    ButtonFlags_MouseButtonLeft = 1 << 0,
    // React on right mouse button
    ButtonFlags_MouseButtonRight = 1 << 1,
    // React on center mouse button
    ButtonFlags_MouseButtonMiddle = 1 << 2,
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
    // hold to repeat
    ButtonFlags_Repeat = 1 << 10,
    // [Internal]
    ButtonFlags_MouseButtonMask =
        ButtonFlags_MouseButtonLeft | ButtonFlags_MouseButtonRight | ButtonFlags_MouseButtonMiddle,
    ButtonFlags_MouseButtonDefault,
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
