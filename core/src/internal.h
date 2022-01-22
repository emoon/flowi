#pragma once

#include "flowi.h"
#include "../include/config.h"
#include "../include/error.h"
#include "style.h"
#include "primitives.h"
#include "vertex_allocator.h"
#include "command_buffer.h"
#include "simd.h"
#include "layout_private.h"

#if defined(FL_FONTLIB_FREETYPE)
#include <freetype/freetype.h>
#endif

#define FL_FONTS_MAX 32
#define FL_MAX_STYLES 128
#define FL_MAX_LAYOUTS 256
#define FL_STYLE_DEPTH 128

struct Font;
struct StyleInternal;
struct Atlas;

typedef struct FlGlobalState {
#if defined(FL_FONTLIB_FREETYPE)
	FT_Library ft_library;
#endif
    FlAllocator* global_allocator;

    // Primitive commands
    CommandBuffer primitive_commands;

    // Render commands that is generated for the rendering backend
    CommandBuffer render_commands;

    // TODO: We may have to support more atlases, but right now we have three
    // One for grayscale fonts, one for colored fonts and one for images.
    struct Atlas* mono_fonts_atlas;
    struct Atlas* color_fonts_atlas;
    struct Atlas* images_atlas;

    // TODO: Fix max number of fonts
    struct Font* fonts[FL_FONTS_MAX];
    int font_count;
    u32 temp;
    u16 texture_ids;
} FlGlobalState;

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

typedef struct MouseState {
	FlVec2 pos;
	bool buttons[3];
} MouseState;

typedef struct Rect {
	float x,y,width,height;
} Rect;

// TODO: We can lilkey do this better
typedef struct ItemWithText {
	char text[1024];
	int len;
} ItemWithText;

typedef struct FlContext {
	// hash of the full context. Use for to skip rendering if nothing has changed
	//XXH3_state_t context_hash;
	// Previous frames hash. We can check against this to see if anything has changed
	//XXH3_state_t prev_frame_hash;
	FlVec2 cursor;
	// id from the previous frame
	u32 prev_active_item;
	// current id
	u32 active_item;
	// Tracks the mouse state for this frame
	MouseState mouse_state;
	// TODO: Likely need block allocator here instead
	vec128* positions;
	// TODO: Likely need block allocator here instead
	vec128* colors;
	// TODO: Likely need block allocator here instead
	u32* widget_ids;
	// count of all widgets
	int current_font;
	// count of all widgets
	int widget_count;
	// Times with text (push button, labels, etc)
	int items_with_text_count;
	ItemWithText* items_with_text;
	// Active fade actions
	int fade_actions;

	FlGlobalState* global_state;

    // Used for building vertex / index output
	VertexAllocator vertex_allocator;
	LinearAllocator layout_allocator;
    LayoutAreaPrivate* current_layout;

	struct StyleInternal* styles[FL_MAX_STYLES]; // TODO: Dynamic array instead of hard-coded max style
	struct StyleInternal* style_stack[FL_STYLE_DEPTH]; // Having 128 max styles should be enough

	int style_count;
    int layout_count;
	int style_stack_depth;

	int layout_ids;

} FlContext;

