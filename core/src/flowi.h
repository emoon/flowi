#pragma once

#include <stdbool.h>
#include "types.h"
#include "../include/flowi_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLI_INDEX_SIZE 2

struct FliContext;

typedef struct FliVec2 {
    float x, y;
} FliVec2;

// Output data to draw on the GPU
/*
typedef struct FliDrawData {
    FliVertPosColor* pos_color_vertices;
    FliVertPosUvColor* pos_uv_color_vertices;
    FliIdxSize* pos_color_indices;
    FliIdxSize* pos_uv_color_indices;
    int pos_color_triangle_count;
    int pos_uv_color_triangle_count;
} FliDrawData;
*/

struct FliRenderData;
struct FliContext* fli_context_create();

// This to be called before using any other functions
void fli_create();

// To be called before exit
void fli_destroy();

/// Create a push button with the given label
bool fli_button_c(struct FliContext* ctx, const char* label);
bool fli_button_size_c(struct FliContext* ctx, const char* label, FliVec2 size);
bool fli_button_ex_c(struct FliContext* ctx, const char* label, int label_len, FliVec2 Size);

void fli_frame_begin(struct FliContext* ctx);
void fli_frame_end(struct FliContext* ctx);

/// Set the mouse position in window relative pixel positions and button 1/2/3
void fli_set_mouse_pos_state(struct FliContext* ctx, FliVec2 pos, bool b1, bool b2, bool b3);

struct FliRenderData* fli_get_render_data(struct FliContext* ctx);


#ifdef __cplusplus
}
#endif

