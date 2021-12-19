#pragma once

#include <stdbool.h>
#include "types.h"
#include "../include/flowi_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_INDEX_SIZE 2

struct FlContext;
struct FlGlobalState;
struct FlRenderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlVec2 {
    f32 x, y;
} FlVec2;

// TODO: Bunch of settings here including max memory usage etc
typedef struct FlSettings {
    // dummy currently
    int max_memory_usage;
} FlSettings;

// This to be called before using any other functions
struct FlGlobalState* fl_create(const FlSettings* settings);

// To be called before exit
void fl_destroy(struct FlGlobalState* state);

// Create a context for UI updates etc
struct FlContext* fl_context_create(struct FlGlobalState* state);

/// Create a push button with the given label
bool fl_button_c(struct FlContext* ctx, const char* label);
bool fl_button_size_c(struct FlContext* ctx, const char* label, FlVec2 size);
bool fl_button_ex_c(struct FlContext* ctx, const char* label, int label_len, FlVec2 Size);

void fl_frame_begin(struct FlContext* ctx);
void fl_frame_end(struct FlContext* ctx);

void fl_text(struct FlContext* ctx, const char* text);
void fl_text_len(struct FlContext* ctx, const char* text, int text_len);

/// Set the mouse position in window relative pixel positions and button 1/2/3
void fl_set_mouse_pos_state(struct FlContext* ctx, FlVec2 pos, bool b1, bool b2, bool b3);

struct FlRenderData* fl_get_render_data(struct FlContext* ctx);

#ifdef __cplusplus
}
#endif

