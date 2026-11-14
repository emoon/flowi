#pragma once

// GPU backend: the clip stack walked over the single translation pass. Each level holds the effective clip (the
// running intersection down the stack, rooted at the full target), so the current clip is always the top and a
// pop is a depth decrement. Unbalanced push/pop is clamped and diagnosed once per frame.

#include "gpu_ir_to_draw_cmd.h"
#include <ui/render_ir.h>

enum { GPU_CLIP_STACK_DEPTH = 32 };

typedef struct GpuClipStack {
    GpuAabb effective[GPU_CLIP_STACK_DEPTH + 1]; // [0] = full target; [depth] = current
    u32 depth;
    u32 ignored_pushes; // pushes past capacity, matched against pops before the stack unwinds
    bool diagnosed;     // one diagnostic per frame, not one per record
} GpuClipStack;

void gpu_clip_stack_init(GpuClipStack* s, u32 target_width, u32 target_height);
void gpu_clip_stack_push(GpuClipStack* s, const FlCmdClipPush* clip);
void gpu_clip_stack_pop(GpuClipStack* s);
GpuAabb gpu_clip_stack_current(const GpuClipStack* s);

// The root clip: the full target rect.
GpuAabb gpu_clip_stack_root(const GpuClipStack* s);

// Dedup lookup into the draw list's per-frame clip-rect array: exact float match (effective rects from identical
// push sequences are bit-identical). Appends when absent; the array is pre-sized for the worst case.
u32 gpu_clip_rects_find_or_append(GpuDrawList* list, GpuAabb rect);
