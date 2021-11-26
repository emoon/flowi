#pragma once

#include <stdbool.h>
#include "types.h"
#include "../include/flowi_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_INDEX_SIZE 2

struct FlContext;

typedef struct FlVec2 {
    float x, y;
} FlVec2;

struct FlRenderData;
struct FlContext* fl_context_create();

// This to be called before using any other functions
void fl_create();

// To be called before exit
void fl_destroy();

/// Create a push button with the given label
bool fl_button_c(struct FlContext* ctx, const char* label);
bool fl_button_size_c(struct FlContext* ctx, const char* label, FlVec2 size);
bool fl_button_ex_c(struct FlContext* ctx, const char* label, int label_len, FlVec2 Size);

void fl_frame_begin(struct FlContext* ctx);
void fl_frame_end(struct FlContext* ctx);

/// Set the mouse position in window relative pixel positions and button 1/2/3
void fl_set_mouse_pos_state(struct FlContext* ctx, FlVec2 pos, bool b1, bool b2, bool b3);

struct FlRenderData* fl_get_render_data(struct FlContext* ctx);

#ifdef __cplusplus
}
#endif

