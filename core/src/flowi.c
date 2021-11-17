#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "flowi.h"
#include "flowi_render.h"
#include "simd.h"

struct FliContext* g_fli_global_ctx;

typedef struct MouseState {
	FliVec2 pos;
	bool buttons[3];
} MouseState;

typedef struct Rect {
	float x,y,width,height;
} Rect;

// TODO: Block based allocations
#define MAX_CONTROLS 1024
#define WIDGET_SPACING 2
#define MAX_RENDER_COMMANDS 256
#define MAX_RENDER_COMMAND_DATA (1024*1024)

// Temp
#define MAX_VERTS 1024

// We allocate SIMD size extra memory so we can read "out of bounds"
#define MEMORY_PADDING 4

typedef enum ButtonState {
	None,
	Fading,
	Hover,
} ButtonState;

// TODO: We can lilkey do this better
typedef struct ItemWithText {
	char text[1024];
	int len;
} ItemWithText;

// TODO: Revisit data-layout
/*
typedef struct FadeAction {
	// current step_value
	vec128 current_value;
	vec128 step_value;
	u32 widget_id;
	int target;
	int count;
} FadeAction;
*/

typedef struct FliContext {
	// hash of the full context. Use for to skip rendering if nothing has changed
	//XXH3_state_t context_hash;
	// Previous frames hash. We can check against this to see if anything has changed
	//XXH3_state_t prev_frame_hash;
	FliVec2 cursor;
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
	int widget_count;
	// Times with text (push button, labels, etc)
	int items_with_text_count;
	ItemWithText* items_with_text;
	// Active fade actions
	int fade_actions;
	FliDrawData draw_data;

	// Render commands and data for the GPU backend
	FliRenderData render_data;

} FliContext;


// TODO: Allow for custom allocations
FliContext* fli_context_create() {
	FliContext* ctx = aligned_alloc(16, sizeof(FliContext));
	memset(ctx, 0, sizeof(FliContext));

	// TODO: Use custom allocator
	ctx->positions = (vec128*)aligned_alloc(16, sizeof(vec128) * (MAX_CONTROLS + + MEMORY_PADDING));
	ctx->widget_ids = (u32*)aligned_alloc(16, sizeof(u32) * (MAX_CONTROLS + MEMORY_PADDING));
	ctx->items_with_text = (ItemWithText*)aligned_alloc(16, sizeof(ItemWithText) * (MAX_CONTROLS + MEMORY_PADDING));

	// temp
	ctx->draw_data.pos_color_vertices = (FliVertPosColor*)aligned_alloc(16, sizeof(FliVertPosColor) * MAX_VERTS);
	ctx->draw_data.pos_uv_color_vertices = (FliVertPosUvColor*)aligned_alloc(16, sizeof(FliVertPosUvColor) * MAX_VERTS);
	ctx->draw_data.pos_color_indices = (FliIdxSize*)aligned_alloc(16, sizeof(FliIdxSize) * MAX_VERTS * 6);
	ctx->draw_data.pos_uv_color_indices = (FliIdxSize*)aligned_alloc(16, sizeof(FliIdxSize) * MAX_VERTS * 6);

	// temp
	ctx->render_data.render_commands = aligned_alloc(16, MAX_RENDER_COMMANDS);
	ctx->render_data.render_data = alligned_alloc(16, MAX_RENDER_COMMAND_DATA);

	return ctx;
}

// This to be called before using any other functions
void fli_create() {
	g_fli_global_ctx = fli_context_create();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// taken from:
// http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/142054
//
// djb  :: 99.8601 percent coverage (60 collisions out of 42884)
// elf  :: 99.5430 percent coverage (196 collisions out of 42884)
// sdbm :: 100.0000 percent coverage (0 collisions out of 42884) (this is the algo used)
// ...

static u32 str_hash(const char* string, int len) {
    u32 hash = 0;

    const u8* str = (const u8*)string;

	for (int i = 0; i < len; ++i) {
		u32 c = *str++;
        hash = c + (hash << 6) + (hash << 16) - hash;
	}

    return hash & 0x7FFFFFFF;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int add_or_update_control(FliContext* ctx, u32 id, FliVec2 size) {
	// TODO: Vector-based
	// IDEA: Always add/resolve later instead

	FliVec2 cursor = ctx->cursor;
	ctx->cursor.y += size.y + WIDGET_SPACING;

	vec128* positions = ctx->positions;
	const u32* widget_ids = ctx->widget_ids;

	for (int i = 0, count = ctx->widget_count; i < count; ++i) {
		// Vectorize find
		if (id == widget_ids[i]) {
			positions[i] = simd_set_f32(cursor.x, cursor.y, size.x, size.y);
			return i;
		}
	}

	int index = ctx->widget_count++;
	ctx->widget_ids[index] = id;

	return -1;
}

// To be called before exit
void fli_destroy();

void fli_begin_c(FliContext* context) {
	context->cursor.x = 0.0f;
	context->cursor.y = 0.0f;
    //XXH3_64bits_reset(context->context_hash);
}

/// Create a push button with the given label
bool fli_button_c(struct FliContext* context, const char* label) {
	FliVec2 size = { 100.0f, 100.0f };
	int label_len = strlen(label);
	return fli_button_ex_c(context, label, label_len, size);
}

bool fli_button_size_c(struct FliContext* context, const char* label, FliVec2 size) {
	int label_len = strlen(label);
	return fli_button_ex_c(context, label, label_len, size);
}

bool fli_button_ex_c(struct FliContext* ctx, const char* label, int label_len, FliVec2 size) {
	u32 control_id = str_hash(label, label_len);

	if (add_or_update_control(ctx, control_id, size) < 0) {
		int item_index = ctx->items_with_text_count++;
		// TODO: handle out of bounds
		ItemWithText* item = &ctx->items_with_text[item_index];
		memcpy(item->text, label, label_len);
		item->len = label_len;
	}

	if (ctx->active_item == 0 && ctx->mouse_state.buttons[0]) {
		ctx->active_item = control_id;
	}
	/*
    if (ctx->mouse_state.buttons[0] == 0 && ctx->hot_item == control_id && ctx->active_item == control_id) {
        return true;
    }
    */

    return false;
}

// TODO: Vectorize
void fli_set_mouse_pos_state(struct FliContext* ctx, FliVec2 pos, bool b1, bool b2, bool b3) {
	// Tracks the mouse state for this frame
	ctx->mouse_state.pos = pos;
	ctx->mouse_state.buttons[0] = b1;
	ctx->mouse_state.buttons[1] = b2;
	ctx->mouse_state.buttons[2] = b3;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Use block based allocator

static void fli_generate_render_data(struct FliContext* ctx) {
    FliVertPosColor* pos_color_vertices = ctx->draw_data.pos_color_vertices;
    FliIdxSize* pos_color_indices = ctx->draw_data.pos_color_indices;
    Rect* rects = (Rect*)ctx->positions;

    FliIdxSize vertex_id = 0;
    u32 color = 0x0fffff1f;
    u32 vertex_count = 0;

	// TODO: More types etc
	// Vectorize
	for (int i = 0, count = ctx->items_with_text_count; i < count; ++i) {
		Rect r = *rects++;
		r.width = 100.0f;
		r.height = 100.0f;

		float x0 = r.x;
		float y0 = r.y;
		float x1 = x0 + r.width;
		float y1 = y0 + r.height;

		printf("%f %f - %f %f\n", x0, y0, x1, y1);

		// vert 1
		pos_color_vertices[0].x = x0;
		pos_color_vertices[0].y = y0;
		pos_color_vertices[0].color = color;

		pos_color_vertices[1].x = x1;
		pos_color_vertices[1].y = y0;
		pos_color_vertices[1].color = color;

		pos_color_vertices[2].x = x1;
		pos_color_vertices[2].y = y1;
		pos_color_vertices[2].color = color;

		pos_color_vertices[3].x = x0;
		pos_color_vertices[3].y = y1;
		pos_color_vertices[3].color = color;

		// Generate triangles

		pos_color_indices[0] = vertex_id + 0;
		pos_color_indices[1] = vertex_id + 1;
		pos_color_indices[2] = vertex_id + 2;

		pos_color_indices[3] = vertex_id + 0;
		pos_color_indices[4] = vertex_id + 2;
		pos_color_indices[5] = vertex_id + 3;

		vertex_id += 4;
		pos_color_vertices += 4;
		pos_color_indices += 2;

		// generate a quad
	}

	ctx->draw_data.pos_color_triangle_count = vertex_id / 2;
	ctx->draw_data.pos_color_triangle_count = vertex_id / 2;

	// hack add render command

	FliRcSolidTriangles* tri_data = (FliRcSolidTriangles*)ctx->render_commands.render_data;

	tri_data->vertex_buffer = ctx->draw_data.pos_color_vertices;
	tri_data->index_buffer = ctx->draw_data.pos_color_indices;
	tri_data->vertex_count = vertex_id;
	tri_data->triangle_count = vertex_id / 2;

	*cxt->render_commands = (u8)FliRc_RenderTriangles;
	*ctx->render_command_count = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void fli_begin() {
	fli_begin_c(g_fli_global_ctx);
}
*/

void fli_frame_begin(struct FliContext* ctx) {
	ctx->cursor.x = 0;
	ctx->cursor.y = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fli_frame_end(struct FliContext* ctx) {
	fli_generate_render_data(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FliRenderData* fli_get_render_data_get(struct FliContext* ctx) {
	return &ctx->render_data;
}







