#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "flowi.h"

typedef uint32_t u32;
typedef uint8_t u8;

struct FliContext* g_fli_global_ctx;

typedef struct MouseState {
	FliVec2 pos;
	bool buttons[3];
} MouseState;

typedef struct FliContext {
	// hash of the full context. Use for to skip rendering if nothing has changed
	//XXH3_state_t context_hash;
	// Previous frames hash. We can check against this to see if anything has changed
	//XXH3_state_t prev_frame_hash;
	// Tracks the mouse state for this frame
	MouseState mouse_state;
	// TODO: Likely need block allocator here instead
	float* control_positions;
	// TODO: Likely need block allocator here instead
	u32* control_ids;
	// count of all widgets
	int widget_count;
} FliContext;


// TODO: Allow for custom allocations
FliContext* fli_context_create() {
	FliContext* context = aligned_alloc(16, sizeof(FliContext));
	memset(context, 0, sizeof(FliContext));
	return context;
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
//

static void add_or_update_control(FliContext* ctx, u32 id, FliVec2 size) {


}

// To be called before exit
void fli_destroy();

void fli_begin_c(FliContext* context) {
    //XXH3_64bits_reset(context->context_hash);
}

/// Create a push button with the given label
bool fli_button_c(struct FliContext* context, const char* label) {
	FliVec2 size = { 0.0f, 0.0f };
	int label_len = strlen(label);
	return fli_button_ex_c(context, label, label_len, size);
}

bool fli_button_size_c(struct FliContext* context, const char* label, FliVec2 size) {
	int label_len = strlen(label);
	return fli_button_ex_c(context, label, label_len, size);
}

bool fli_button_ex_c(struct FliContext* ctx, const char* label, int label_len, FliVec2 size) {
	u32 control_id = str_hash(label, label_len);

	add_or_update_control(ctx, control_id, size);

	/*
	if (ctx->active_item == 0 && ctx->mouse_state->buttons[0]) {
		ctx->active_item = control_id;
	}

    if (ctx->mouse_state->buttons[0] == 0 && ctx->hot_item == control_id && ctx->active_item == control_id) {
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

void fli_begin() {
	fli_begin_c(g_fli_global_ctx);
}


