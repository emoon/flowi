#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FliContext;

typedef struct FliVec2 {
    float x, y;
} FliVec2;

extern struct FliContext* g_fli_global_ctx;

struct FliContext* fli_context_create();

// This to be called before using any other functions
void fli_create();

// To be called before exit
void fli_destroy();

/// Create a push button with the given label
bool fli_button_c(struct FliContext* ctx, const char* label);
bool fli_button_size_c(struct FliContext* ctx, const char* label, FliVec2 size);
bool fli_button_ex_c(struct FliContext* ctx, const char* label, int label_len, FliVec2 Size);

/// Set the mouse position in window relative pixel positions and button 1/2/3
void fli_set_mouse_pos_state(struct FliContext* ctx, FliVec2 pos, bool b1, bool b2, bool b3);

#ifdef __cplusplus
}
#endif

