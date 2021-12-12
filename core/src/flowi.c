#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "flowi.h"
#include "../include/flowi_render.h"
#include "simd.h"
#include "internal.h"
#include "render.h"

#if defined(_MSC_VER)
#include <malloc.h>
#undef aligned_alloc
#define aligned_alloc(align, size) _aligned_malloc(size, align)
#endif

struct FlContext* g_fl_global_ctx = NULL;
struct FlGlobalState* g_state = NULL;

//extern FliGlobalCtx* g_global_ctx;

// Output data to draw on the GPU

typedef struct FlDrawData {
    FlVertPosColor* pos_color_vertices;
    FlVertPosUvColor* pos_uv_color_vertices;
    FlIdxSize* pos_color_indices;
    FlIdxSize* pos_uv_color_indices;
    int pos_color_triangle_count;
    int pos_uv_color_triangle_count;
} FlDrawData;

typedef struct MouseState {
	FlVec2 pos;
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
	int widget_count;
	// Times with text (push button, labels, etc)
	int items_with_text_count;
	ItemWithText* items_with_text;
	// Active fade actions
	int fade_actions;
	FlDrawData draw_data;

	FlGlobalState* global_state;
	BuildRenderState* build_state;

	// Render commands and data for the GPU backend
	FlRenderData render_data_out;

} FlContext;


// TODO: Allow for custom allocations
FlContext* fl_context_create(struct FlGlobalState* state) {
	FlContext* ctx = aligned_alloc(16, sizeof(FlContext));
	memset(ctx, 0, sizeof(FlContext));

	// TODO: Use custom allocator
	ctx->positions = (vec128*)aligned_alloc(16, sizeof(vec128) * (MAX_CONTROLS + + MEMORY_PADDING));
	ctx->widget_ids = (u32*)aligned_alloc(16, sizeof(u32) * (MAX_CONTROLS + MEMORY_PADDING));
	ctx->items_with_text = (ItemWithText*)aligned_alloc(16, sizeof(ItemWithText) * (MAX_CONTROLS + MEMORY_PADDING));

	// temp
	ctx->draw_data.pos_color_vertices = (FlVertPosColor*)aligned_alloc(16, sizeof(FlVertPosColor) * MAX_VERTS);
	ctx->draw_data.pos_uv_color_vertices = (FlVertPosUvColor*)aligned_alloc(16, sizeof(FlVertPosUvColor) * MAX_VERTS);
	ctx->draw_data.pos_color_indices = (FlIdxSize*)aligned_alloc(16, sizeof(FlIdxSize) * MAX_VERTS * 6);
	ctx->draw_data.pos_uv_color_indices = (FlIdxSize*)aligned_alloc(16, sizeof(FlIdxSize) * MAX_VERTS * 6);

	// TODO: Fixup
	ctx->build_state = &state->render_data;
	ctx->global_state = state;

	// temp
	//ctx->render_data.render_commands = aligned_alloc(16, MAX_RENDER_COMMANDS);
	//ctx->render_data.render_data = aligned_alloc(16, MAX_RENDER_COMMAND_DATA);

	return ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This to be called before using any other functions

struct FlGlobalState* fl_create(const FlSettings* settings) {
	// TODO: Use local allocator
	g_state = (FlGlobalState*)calloc(1, sizeof(FlGlobalState));

	g_fl_global_ctx = fl_context_create(g_state);

	// TODO: Custom allocator
	u8* render_data = (u8*)malloc(1024 * 1024);
	u8* render_commands = (u8*)malloc(10 * 1024);

	g_state->render_data.render_data = render_data;
	g_state->render_data.render_commands = render_commands;
	g_state->render_data.start_render_data = render_data;
	g_state->render_data.start_render_commands = render_commands;
	g_state->render_data.end_render_data = render_data + (1024 * 1024);
	g_state->render_data.end_render_commands = render_commands + (10 * 1024);

	Font_init(g_state);

	return g_state;
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

static int add_or_update_control(FlContext* ctx, u32 id, FlVec2 size) {
	// TODO: Vector-based
	// IDEA: Always add/resolve later instead

	FlVec2 cursor = ctx->cursor;
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
void fl_destroy();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_begin_c(FlContext* context) {
	context->cursor.x = 0.0f;
	context->cursor.y = 0.0f;
    //XXH3_64bits_reset(context->context_hash);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Create a push button with the given label

bool fl_button_c(struct FlContext* context, const char* label) {
	FlVec2 size = { 100.0f, 100.0f };
	int label_len = strlen(label);
	return fl_button_ex_c(context, label, label_len, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_button_size_c(struct FlContext* context, const char* label, FlVec2 size) {
	int label_len = strlen(label);
	return fl_button_ex_c(context, label, label_len, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_button_ex_c(struct FlContext* ctx, const char* label, int label_len, FlVec2 size) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Vectorize

void fl_set_mouse_pos_state(struct FlContext* ctx, FlVec2 pos, bool b1, bool b2, bool b3) {
	// Tracks the mouse state for this frame
	ctx->mouse_state.pos = pos;
	ctx->mouse_state.buttons[0] = b1;
	ctx->mouse_state.buttons[1] = b2;
	ctx->mouse_state.buttons[2] = b3;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Use block based allocator

/*
static void fl_generate_render_data(struct FlContext* ctx) {
    FlVertPosColor* pos_color_vertices = ctx->draw_data.pos_color_vertices;
    FlIdxSize* pos_color_indices = ctx->draw_data.pos_color_indices;
    Rect* rects = (Rect*)ctx->positions;

    FlIdxSize vertex_id = 0;
    u32 color = 0x0fffff1f;

	// TODO: More types etc
	// Vectorize
	for (int i = 0, count = ctx->items_with_text_count; i < count; ++i) {
		Rect r = *rects++;
		r.width = 100.0f;
		r.height = 100.0f;

		float x0 = r.x + 10.0f;
		float y0 = r.y + 10.0f;
		float x1 = x0 + r.width + 10.0f;
		float y1 = y0 + r.height + 10.0f;

		//printf("%f %f - %f %f\n", x0, y0, x1, y1);

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
		pos_color_indices += 6;

		// generate a quad
	}

	ctx->draw_data.pos_color_triangle_count = vertex_id / 2;

	FlRcSolidTriangles* tri_data = Render_render_flat_triangles_static(
		ctx->global_state,
    	ctx->draw_data.pos_color_vertices,
    	ctx->draw_data.pos_color_indices);

	tri_data->vertex_count = vertex_id;
	tri_data->index_count = 6;
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fl_generate_render_data_2(struct FlContext* ctx) {
    FlVertPosUvColor* pos_uv_color_vertices = ctx->draw_data.pos_uv_color_vertices;
    FlIdxSize* indices = ctx->draw_data.pos_color_indices;
    //Rect* rects = (Rect*)ctx->positions;

    FlIdxSize vertex_id = 0;
    u32 color = 0x0fffff1f;

	// TODO: More types etc
	// Vectorize
	for (int i = 0, count = ctx->items_with_text_count; i < count; ++i) {
		//Rect r;// = *rects++;
		//r.width = 640.0f;
		//r.height = 400.0f;

		/*
		float x0 = r.x;
		float y0 = r.y;
		float x1 = x0 + r.width;
		float y1 = y0 + r.height;
		*/

		/*
		simd = x0_y0_x1_y1
		simd = u0v0_u1v1....
		simd = c_c_c_c

		output

		x0_y0_u0v0_c
		x1_y0_u1v0_c
		x1_y1_u1v1_c
		x0_y1_u0v1_c

		*/

		float x0 = 0.0f;
		float y0 = 0.0f;
		float x1 = 640.0f;
		float y1 = 400.0f;

		// vert 1
		pos_uv_color_vertices[0].x = x0;
		pos_uv_color_vertices[0].y = y0;
		pos_uv_color_vertices[0].u = 0;
		pos_uv_color_vertices[0].v = 0;
		pos_uv_color_vertices[0].color = color;

		pos_uv_color_vertices[1].x = x1;
		pos_uv_color_vertices[1].y = y0;
		pos_uv_color_vertices[1].u = 1024;
		pos_uv_color_vertices[1].v = 0;
		pos_uv_color_vertices[1].color = color;

		pos_uv_color_vertices[2].x = x1;
		pos_uv_color_vertices[2].y = y1;
		pos_uv_color_vertices[2].u = 1024;
		pos_uv_color_vertices[2].v = 1024;
		pos_uv_color_vertices[2].color = color;

		pos_uv_color_vertices[3].x = x0;
		pos_uv_color_vertices[3].y = y1;
		pos_uv_color_vertices[3].u = 0;
		pos_uv_color_vertices[3].v = 1024;
		pos_uv_color_vertices[3].color = color;

		// Generate triangles

		indices[0] = vertex_id + 0;
		indices[1] = vertex_id + 1;
		indices[2] = vertex_id + 2;

		indices[3] = vertex_id + 0;
		indices[4] = vertex_id + 2;
		indices[5] = vertex_id + 3;

		vertex_id += 4;
		pos_uv_color_vertices += 4;
		indices += 6;

		// generate a quad
	}

	FlRcTexturedTriangles* tri_data = Render_render_texture_triangles_static(
		ctx->global_state,
    	ctx->draw_data.pos_uv_color_vertices,
    	ctx->draw_data.pos_color_indices);

	tri_data->vertex_count = vertex_id;
	tri_data->index_count = 6;

	// TODO: Fix me
	tri_data->texture_id = 0;

	//ctx->render_data.count = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
static void generate_text_mesh_data(struct FlContext* ctx) {
    FlVertPosUvColor* pos_uv_color_vertices = ctx->draw_data.pos_uv_color_vertices;
    FlIdxSize* indices = ctx->draw_data.pos_color_indices;
    //Rect* rects = (Rect*)ctx->positions;

    FlIdxSize vertex_id = 0;
    u32 color = 0x0fffff1f;

	// TODO: More types etc
	// Vectorize
	for (int i = 0, count = ctx->items_with_text_count; i < count; ++i) {
		//Rect r = *rects++;
		//r.width = 640.0f;
		//r.height = 400.0f;

		/*
		float x0 = r.x;
		float y0 = r.y;
		float x1 = x0 + r.width;
		float y1 = y0 + r.height;
		*/

		/*
		simd = x0_y0_x1_y1
		simd = u0v0_u1v1....
		simd = c_c_c_c

		output

		x0_y0_u0v0_c
		x1_y0_u1v0_c
		x1_y1_u1v1_c
		x0_y1_u0v1_c

		*/

		float x0 = 0.0f;
		float y0 = 0.0f;
		float x1 = 640.0f;
		float y1 = 400.0f;

		// vert 1
		pos_uv_color_vertices[0].x = x0;
		pos_uv_color_vertices[0].y = y0;
		pos_uv_color_vertices[0].u = 0;
		pos_uv_color_vertices[0].v = 0;
		pos_uv_color_vertices[0].color = color;

		pos_uv_color_vertices[1].x = x1;
		pos_uv_color_vertices[1].y = y0;
		pos_uv_color_vertices[1].u = 256;
		pos_uv_color_vertices[1].v = 0;
		pos_uv_color_vertices[1].color = color;

		pos_uv_color_vertices[2].x = x1;
		pos_uv_color_vertices[2].y = y1;
		pos_uv_color_vertices[2].u = 256;
		pos_uv_color_vertices[2].v = 256;
		pos_uv_color_vertices[2].color = color;

		pos_uv_color_vertices[3].x = x0;
		pos_uv_color_vertices[3].y = y1;
		pos_uv_color_vertices[3].u = 0;
		pos_uv_color_vertices[3].v = 256;
		pos_uv_color_vertices[3].color = color;

		// Generate triangles

		indices[0] = vertex_id + 0;
		indices[1] = vertex_id + 1;
		indices[2] = vertex_id + 2;

		indices[3] = vertex_id + 0;
		indices[4] = vertex_id + 2;
		indices[5] = vertex_id + 3;

		vertex_id += 4;
		pos_uv_color_vertices += 4;
		indices += 6;

		// generate a quad
	}

	FlRcTexturedTriangles* tri_data = Render_render_texture_triangles_static(
		ctx->global_state,
    	ctx->draw_data.pos_uv_color_vertices,
    	ctx->draw_data.pos_color_indices);

	tri_data->vertex_count = vertex_id;
	tri_data->index_count = 6;

	// TODO: Fix me
	tri_data->texture_id = 0;

	//ctx->render_data.count = 1;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void fl_begin() {
	fl_begin_c(g_fl_global_ctx);
}
*/

void fl_frame_begin(struct FlContext* ctx) {
	ctx->cursor.x = 0;
	ctx->cursor.y = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_frame_end(struct FlContext* ctx) {
	//fl_generate_render_data(ctx);
	fl_generate_render_data_2(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlRenderData* fl_get_render_data(struct FlContext* ctx) {
	BuildRenderState* render_state = &ctx->global_state->render_data;

	u8* start_render_commands = render_state->start_render_commands;
	u8* render_commands = render_state->render_commands;

	ctx->render_data_out.render_commands = start_render_commands;
	ctx->render_data_out.render_data = render_state->start_render_data;
	ctx->render_data_out.count = (u32)((uintptr_t)render_commands - (uintptr_t)start_render_commands);

	//printf("commands %d\n", ctx->render_data_out.count);

	// Reset back the pointers

	render_state->render_data = render_state->start_render_data;
	render_state->render_commands = render_state->start_render_commands;

	return &ctx->render_data_out;
}

