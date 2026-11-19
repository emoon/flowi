#pragma once

// Shared helpers for the unit_tests_render_ir_*.c translation units: painter frames, IR stream walking and the
// software and GPU decode of a recorded IR range.

#include "unit_tests_common.h"

// Runs a helper that takes the case's utest_result, and returns from the enclosing case once it has failed.
#define IR_EXPECT(helper, ...)                        \
    do {                                              \
        helper(utest_result, __VA_ARGS__);            \
        if (*utest_result != UTEST_TEST_PASSED) {     \
            return;                                   \
        }                                             \
    } while (0)

// Painter frames.

static inline PainterFrame test_frame(FlArena* arena, FlFontCache* cache, float ui_scale, float text_scale) {
    return (PainterFrame) { .font_cache = cache, .ui_scale = ui_scale, .text_scale = text_scale, .state_arena = arena };
}

static inline Painter* test_painter_begin(FlArena* arena, FlFontCache* cache, float ui_scale, float text_scale) {
    Painter* p = painter_create(arena);
    painter_begin_frame(p, test_frame(arena, cache, ui_scale, text_scale));
    return p;
}

// Paint spans.

// The first record of the span the scope left in slot, or nullptr when it attached none.
static inline const FlCmdHeader* span_first_record(const void* slot) {
    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    return (span != nullptr && span->first != nullptr) ? (const FlCmdHeader*)span->first->ir : nullptr;
}

// IR records.

static inline const FlCmdHeader* ir_next(const FlCmdHeader* h) {
    return (const FlCmdHeader*)((const u8*)h + h->byte_size);
}

static inline bool ir_has_flag(const FlCmdHeader* h, u32 flag) {
    return (h->flags & flag) != 0;
}

static inline FlRect rect_xywh(float x, float y, float w, float h) {
    return (FlRect) { .x = x, .y = y, .w = w, .h = h };
}

static inline bool rect_is(FlRect got, FlRect want) {
    return got.x == want.x && got.y == want.y && got.w == want.w && got.h == want.h;
}

static inline bool radius_is(FlCornerRadius got, FlCornerRadius want) {
    return got.tl == want.tl && got.tr == want.tr && got.bl == want.bl && got.br == want.br;
}

// Asserts the walk over a record stream ends at end, overshooting it by less than the 8-byte record stride.
static inline void expect_walk_ends_at(int* utest_result, const FlCmdHeader* h, const u8* end, const char* msg) {
    ASSERT_TRUE_MSG((const u8*)h >= end && (const u8*)h - end < 8, msg);
}

// A byte range of an IR arena.
typedef struct IrRange {
    const u8* start;
    size_t bytes;
} IrRange;

// Aligns the arena to the record stride and returns where the next record will start.
static inline const u8* ir_mark(FlArena* arena) {
    fl_arena_align(arena, 8);
    return (const u8*)fl_arena_current_pos(arena);
}

// The range from start to the arena's current position.
static inline IrRange ir_range_since(FlArena* arena, const u8* start) {
    return (IrRange) { .start = start, .bytes = (size_t)((const u8*)fl_arena_current_pos(arena) - start) };
}

// Decode.

// The arenas a software decode of an IR range needs.
typedef struct SwRig {
    FlArena* ir;
    FlArena* commands;
    FlArena* state;
} SwRig;

static inline SwRig sw_rig_new(void) {
    return (SwRig) { .ir = arena_new(), .commands = arena_new(), .state = arena_new() };
}

static inline void sw_rig_destroy(SwRig* rig) {
    fl_arena_destroy(rig->ir);
    fl_arena_destroy(rig->commands);
    fl_arena_destroy(rig->state);
}

static inline UiRenderCommandsArray sw_translate_ir(const SwRig* rig, IrRange range, u8 global_alpha) {
    return sw_translate(rig->commands, rig->state, range.start, range.bytes, global_alpha);
}

// Decodes range with the GPU backend. target supplies the global alpha and target size.
static inline GpuDrawList gpu_decode_ir(FlArena* commands_arena, IrRange range, GpuIrInput target) {
    target.commands = range.start;
    target.bytes = range.bytes;
    return gpu_ir_to_draw_cmds(commands_arena, &target);
}

// Loads the glyph test font at its 32px authored size.
static inline FlFontId load_glyph_test_font(FlFontCache* cache) {
    return font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
}
