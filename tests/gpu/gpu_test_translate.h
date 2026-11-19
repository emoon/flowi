#pragma once

// Shared context and comparison helpers for the CPU translator unit tests.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

typedef struct TrCtx {
    FlArena* ir_arena;
    FlArena* out_arena;
    GpuDrawList list;
    void* start;
    GpuImageAtlas atlas;
    f32 atlas_x;
    f32 atlas_y;
    GpuImageSlots slots;
    GpuGlyphAtlas glyphs;
    GpuSvgAtlas svg;
    GpuSvgAtlas svg_color;
    const GpuSvgEntry* svg_cell;
    const GpuSvgEntry* svg_color_cell;
} TrCtx;

static inline void tr_begin(TrCtx* c) {
    c->start = ir_rewind_begin(c->ir_arena);
}

static inline void tr_run(TrCtx* c, u8 global_alpha) {
    c->list = translate(c->out_arena, c->ir_arena, c->start, global_alpha);
}

static inline void tr_run_atlas(TrCtx* c, u8 global_alpha) {
    c->list = translate_with(c->out_arena, (TranslateJob) { .ir_arena = c->ir_arena,
                                                            .start = c->start,
                                                            .global_alpha = global_alpha,
                                                            .atlas = &c->atlas,
                                                            .slots = nullptr });
}

// The translation input over the context's current stream; callers add the atlas pointers a case needs.
static inline GpuIrInput tr_input(const TrCtx* c, u8 global_alpha) {
    return (GpuIrInput) { .commands = c->start,
                          .bytes = ir_bytes(c->ir_arena, c->start),
                          .global_alpha = global_alpha,
                          .target_width = TR_TARGET,
                          .target_height = TR_TARGET };
}

static inline void tr_run_input(TrCtx* c, const GpuIrInput* in) {
    c->list = gpu_ir_to_draw_cmds(c->out_arena, in);
}

static inline bool rect_words_are(const GpuDrawCmd* cmd, FlRect want) {
    return cmd->rect[0] == want.x && cmd->rect[1] == want.y && cmd->rect[2] == want.w && cmd->rect[3] == want.h;
}

static inline bool radii_are(const GpuDrawCmd* cmd, FlRect want) {
    return cmd->radius[0] == want.x && cmd->radius[1] == want.y && cmd->radius[2] == want.w && cmd->radius[3] == want.h;
}

static inline bool aabb_is(const GpuAabb* a, GpuAabb want) {
    return a->x0 == want.x0 && a->y0 == want.y0 && a->x1 == want.x1 && a->y1 == want.y1;
}

typedef int (*TrCheckFn)(TrCtx*);

typedef struct TrSuite {
    const TrCheckFn* checks;
    size_t count;
    const char* name;
} TrSuite;

// Runs each check against a fresh pair of arenas, then prints the suite's PASS line.
static inline int run_tr_suite(const TrSuite* suite) {
    TrCtx c = { .ir_arena = arena_new(), .out_arena = arena_new() };
    for (size_t i = 0; i < suite->count; i++) {
        if (suite->checks[i](&c) != 0) {
            return 1;
        }
    }
    fl_arena_destroy(c.out_arena);
    fl_arena_destroy(c.ir_arena);
    printf("gpu_smoke_test: %s PASS\n", suite->name);
    return 0;
}

#define RUN_TR_SUITE(label, ...)                                            \
    do {                                                                    \
        static const TrCheckFn checks_[] = { __VA_ARGS__ };                 \
        const TrSuite suite_ = { checks_, sizeof_array(checks_), (label) }; \
        return run_tr_suite(&suite_);                                       \
    } while (0)

#endif // !defined(__APPLE__)
