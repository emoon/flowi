#include "gpu_clip_stack.h"
#include "gpu_aabb.h"
#include <core/core.h>
#include <core/log.h>

void gpu_clip_stack_init(GpuClipStack* s, u32 target_width, u32 target_height) {
    s->effective[0] = (GpuAabb) { 0.0f, 0.0f, (f32)target_width, (f32)target_height };
    s->depth = 0;
    s->ignored_pushes = 0;
    s->diagnosed = false;
}

static bool first_diagnosis(GpuClipStack* s) {
    bool first = !s->diagnosed;
    s->diagnosed = true;
    return first;
}

void gpu_clip_stack_push(GpuClipStack* s, const FlCmdClipPush* clip) {
    if (s->depth >= GPU_CLIP_STACK_DEPTH) {
        s->ignored_pushes++;
        if (first_diagnosis(s)) {
            log_error("gpu backend: clip stack deeper than %d - ignoring deeper pushes", GPU_CLIP_STACK_DEPTH);
        }
        return;
    }
    s->effective[s->depth + 1] = gpu_aabb_intersect(s->effective[s->depth], gpu_aabb_from_rect(clip->rect));
    s->depth++;
}

void gpu_clip_stack_pop(GpuClipStack* s) {
    if (s->ignored_pushes > 0) {
        s->ignored_pushes--;
        return;
    }
    if (s->depth == 0) {
        if (first_diagnosis(s)) {
            log_error("gpu backend: clip pop on an empty stack - ignored");
        }
        return;
    }
    s->depth--;
}

GpuAabb gpu_clip_stack_current(const GpuClipStack* s) {
    return s->effective[s->depth];
}

GpuAabb gpu_clip_stack_root(const GpuClipStack* s) {
    return s->effective[0];
}

static bool aabb_equal(GpuAabb a, GpuAabb b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

u32 gpu_clip_rects_find_or_append(GpuDrawList* list, GpuAabb rect) {
    for_count(i, list->clip_count) {
        if (aabb_equal(list->clips[i], rect)) {
            return i;
        }
    }
    list->clips[list->clip_count] = rect;
    return list->clip_count++;
}
