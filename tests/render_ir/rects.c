// render-IR batched rect tests

#include "render_ir/common.h"

// One batched painter_rects call leaves exactly the records a painter_rect loop leaves. The backends only see
// the IR, so identical bytes mean identical output on every backend. The cases compare the span (UI scaling,
// effect), the spliced frame stream (Canvas z-order, clipping) and each backend's decode of that stream (alpha
// compositing inputs).

#define RECT_BATCH_COUNT 40

// Overlapping rects with varying fractional geometry and alpha, so call order and compositing both matter.
static void rect_batch_fill(FlColoredRect* rects, u32 count, float seed) {
    for_count(i, count) {
        float f = (float)i;
        rects[i]
            = (FlColoredRect) { seed + f * 1.5f, 3.25f + f * 0.5f, 6.0f + f * 0.125f, 20.0f - f * 0.25f,
                                (FlColor) { (u8)(40 + i * 5), (u8)(200 - i * 3), (u8)(90 + i), (u8)(255 - i * 6) } };
    }
}

// The scalar reference: the same rects through painter_rect, one call each, in order.
static void rect_batch_scalar(Painter* p, const FlColoredRect* rects, u32 count) {
    for_count(i, count) {
        painter_rect(p, (FlRect) { .x = rects[i].x, .y = rects[i].y, .w = rects[i].w, .h = rects[i].h },
                     rects[i].color);
    }
}

static void paint_edge(Painter* p, float at, FlColor edge) {
    painter_rect(p, rect_xywh(at, at, 1.0f, 1.0f), edge);
}

// A record before and after the rects: the rects join the surrounding segment.
static void record_scalar_scope(Painter* p, void** slot, FlEffect shadow, const FlColoredRect* rects) {
    const FlColor edge = { 1, 2, 3, 255 };
    painter_scope_push(p, slot);
    painter_set_effect(p, shadow);
    paint_edge(p, 0.0f, edge);
    rect_batch_scalar(p, rects, RECT_BATCH_COUNT);
    paint_edge(p, 9.0f, edge);
    painter_scope_pop(p);
}

static void record_batch_scope(Painter* p, void** slot, FlEffect shadow, const FlColoredRect* rects) {
    const FlColor edge = { 1, 2, 3, 255 };
    painter_scope_push(p, slot);
    painter_set_effect(p, shadow);
    paint_edge(p, 0.0f, edge);
    painter_rects(p, rects, RECT_BATCH_COUNT);
    paint_edge(p, 9.0f, edge);
    painter_scope_pop(p);
}

static void expect_batch_matches_scalar_span(int* utest_result, const FlPaintSpan* scalar, const FlPaintSpan* batch) {
    ASSERT_TRUE_MSG(scalar != nullptr && batch != nullptr, "both scopes recorded a span");
    ASSERT_TRUE_MSG(scalar->first == scalar->last && batch->first == batch->last,
                    "each stream is one coalesced segment");
    const void* scalar_ir = scalar->first->ir;
    ASSERT_EQ_MSG(batch->first->size, scalar->first->size, "the batch records exactly as many bytes as the loop");
    ASSERT_TRUE_MSG(memcmp(batch->first->ir, scalar_ir, scalar->first->size) == 0,
                    "the batch's records are byte-identical to the scalar loop's (scaling and effect included)");
}

// The first batched record is a plain scaled fill rect carrying the scope's effect.
static void expect_first_batched_record(int* utest_result, const FlPaintSpan* batch) {
    const FlCmdHeader* h = ir_next((const FlCmdHeader*)batch->first->ir);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && ir_has_flag(h, FL_HAS_EFFECT), "batched rects carry the effect");
    const FlCmdFillRect* first = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(first->rect.x == 3.0f && first->rect.y == 4.875f, "batched positions scale by ui_scale");
    ASSERT_TRUE_MSG(first->corner[0].a == 255 && first->corner[3].a == 255, "color lands in all four corners");
}

UTEST(render_ir, painter_rects_emit_the_scalar_record_stream) {
    FlArena* arena = arena_new();
    Painter* p = test_painter_begin(arena, nullptr, 1.5f, 1.0f); // fractional UI scale

    FlColoredRect rects[RECT_BATCH_COUNT];
    rect_batch_fill(rects, RECT_BATCH_COUNT, 2.0f);
    const FlEffect shadow = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_y = 4.0f, .blur = 9.0f };

    void* scalar_slot = nullptr;
    void* batch_slot = nullptr;
    record_scalar_scope(p, &scalar_slot, shadow, rects);
    record_batch_scope(p, &batch_slot, shadow, rects);

    const FlPaintSpan* scalar = (const FlPaintSpan*)scalar_slot;
    const FlPaintSpan* batch = (const FlPaintSpan*)batch_slot;
    IR_EXPECT(expect_batch_matches_scalar_span, scalar, batch);
    IR_EXPECT(expect_first_batched_record, batch);

    fl_arena_destroy(arena);
}

UTEST(render_ir, painter_rects_noop_cases_record_nothing) {
    FlArena* arena = arena_new();
    Painter* p = test_painter_begin(arena, nullptr, 1.0f, 1.0f);
    FlColoredRect one = { 0.0f, 0.0f, 1.0f, 1.0f, { 1, 2, 3, 4 } };

    painter_rects(p, &one, 1); // no open scope

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_rects(p, nullptr, 1);
    painter_rects(p, &one, 0);
    painter_scope_pop(p);
    ASSERT_TRUE_MSG(slot == nullptr, "a null or empty batch never attaches a span, like an empty scalar loop");

    fl_arena_destroy(arena);
}

// The painter scopes of two sibling canvases, recorded as a scalar loop and as a batch.
typedef struct CanvasSlots {
    void* scalar_a;
    void* scalar_b;
    void* batch_a;
    void* batch_b;
} CanvasSlots;

static void record_canvas_scopes(Painter* p, CanvasSlots* slots, const FlColoredRect* a, const FlColoredRect* b) {
    painter_scope_push(p, &slots->scalar_a);
    rect_batch_scalar(p, a, RECT_BATCH_COUNT);
    painter_scope_pop(p);
    painter_scope_push(p, &slots->scalar_b);
    rect_batch_scalar(p, b, RECT_BATCH_COUNT);
    painter_scope_pop(p);
    painter_scope_push(p, &slots->batch_a);
    painter_rects(p, a, RECT_BATCH_COUNT);
    painter_scope_pop(p);
    painter_scope_push(p, &slots->batch_b);
    painter_rects(p, b, RECT_BATCH_COUNT);
    painter_scope_pop(p);
}

// Canvas A under a scissor, canvas B declared later (z-order: B over A), both at fractional origins.
static void build_canvas_commands(Clay_RenderCommand cmds[4], void* canvas_a, void* canvas_b) {
    const Clay_RenderCommand canvas_cmds[4] = {
        { .commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START, .boundingBox = { 10.5f, 8.25f, 40.0f, 30.0f } },
        { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
          .boundingBox = { 10.5f, 8.25f, 80.0f, 60.0f },
          .renderData = { .custom = { .customData = canvas_a } } },
        { .commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END },
        { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
          .boundingBox = { 30.25f, 20.5f, 80.0f, 60.0f },
          .renderData = { .custom = { .customData = canvas_b } } },
    };
    memcpy(cmds, canvas_cmds, sizeof(canvas_cmds));
}

static IrRange splice_frame(FlArena* out, Clay_RenderCommand* cmds) {
    const u8* start = ir_mark(out);
    fl_clay_to_render_ir(out, nullptr, cmds, 4);
    return ir_range_since(out, start);
}

static void expect_frame_streams_match(int* utest_result, IrRange scalar, IrRange batch) {
    ASSERT_TRUE_MSG(scalar.bytes > 0, "the scalar frame produced IR");
    ASSERT_EQ_MSG(batch.bytes, scalar.bytes, "the batched frame has the scalar frame's size");
    ASSERT_TRUE_MSG(memcmp(batch.start, scalar.start, scalar.bytes) == 0,
                    "scissor, z-order and origin splicing give a byte-identical frame stream");
}

// Alpha compositing happens in the backend on the decoded commands, so the decode has to agree too.
static void expect_sw_decodes_match(int* utest_result, const SwRig* rig, IrRange scalar, IrRange batch) {
    UiRenderCommandsArray sw_scalar = sw_translate_ir(rig, scalar, 255);
    UiRenderCommandsArray sw_batch = sw_translate_ir(rig, batch, 255);
    ASSERT_TRUE_MSG(sw_scalar.count >= 2 * RECT_BATCH_COUNT, "the software decode kept every rect");
    ASSERT_EQ_MSG(sw_batch.count, sw_scalar.count, "software decode: same command count");
    ASSERT_TRUE_MSG(memcmp(sw_batch.commands, sw_scalar.commands, sizeof(RenderCommand) * (size_t)sw_scalar.count) == 0,
                    "software decode: identical commands");
}

static void expect_gpu_decodes_match(int* utest_result, FlArena* commands_arena, IrRange scalar, IrRange batch) {
    const GpuIrInput target = { .global_alpha = 255, .target_width = 1920, .target_height = 1080 };
    GpuDrawList gpu_scalar = gpu_decode_ir(commands_arena, scalar, target);
    GpuDrawList gpu_batch = gpu_decode_ir(commands_arena, batch, target);
    ASSERT_TRUE_MSG(gpu_scalar.count >= RECT_BATCH_COUNT,
                    "the GPU decode kept the unclipped canvas (the scissor culls part of the other)");
    ASSERT_EQ_MSG(gpu_batch.count, gpu_scalar.count, "GPU decode: same draw count");
    ASSERT_TRUE_MSG(memcmp(gpu_batch.cmds, gpu_scalar.cmds, sizeof(GpuDrawCmd) * (size_t)gpu_scalar.count) == 0,
                    "GPU decode: identical draw commands");
}

// Splice the spans of two sibling canvases (one under a scissor, at fractional origins) and compare the frame
// IR and both backends' decode of it between the scalar and the batched recording.
UTEST(render_ir, painter_rects_frame_stream_matches_scalar_loop) {
    FlArena* arena = arena_new();
    Painter* p = test_painter_begin(arena, nullptr, 2.0f, 1.0f);

    FlColoredRect a_rects[RECT_BATCH_COUNT];
    FlColoredRect b_rects[RECT_BATCH_COUNT];
    rect_batch_fill(a_rects, RECT_BATCH_COUNT, 0.0f);
    rect_batch_fill(b_rects, RECT_BATCH_COUNT, 5.0f);

    CanvasSlots slots = { 0 };
    record_canvas_scopes(p, &slots, a_rects, b_rects);

    Clay_RenderCommand scalar_cmds[4];
    Clay_RenderCommand batch_cmds[4];
    build_canvas_commands(scalar_cmds, slots.scalar_a, slots.scalar_b);
    build_canvas_commands(batch_cmds, slots.batch_a, slots.batch_b);

    SwRig rig = sw_rig_new();
    IrRange scalar = splice_frame(rig.ir, scalar_cmds);
    IrRange batch = splice_frame(rig.ir, batch_cmds);

    IR_EXPECT(expect_frame_streams_match, scalar, batch);
    IR_EXPECT(expect_sw_decodes_match, &rig, scalar, batch);
    IR_EXPECT(expect_gpu_decodes_match, rig.commands, scalar, batch);

    sw_rig_destroy(&rig);
    fl_arena_destroy(arena);
}
