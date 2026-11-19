// GPU smoke test: IR to draw-command translator and effect translator unit tests.

#include "gpu_test_translate.h"

#if !defined(__APPLE__)

// Runs the translation off the test thread so a regressed zero-byte_size guard hangs this worker instead of
// the test process.
typedef struct MalformedTranslateJob {
    FlArena* out_arena;
    const void* commands;
    size_t bytes;
    GpuDrawList list;
    atomic_bool done;
} MalformedTranslateJob;

static void* malformed_translate_thread(void* user_data) {
    MalformedTranslateJob* job = (MalformedTranslateJob*)user_data;
    job->list = gpu_ir_to_draw_cmds(job->out_arena, &(GpuIrInput) { .commands = job->commands,
                                                                    .bytes = job->bytes,
                                                                    .global_alpha = 255,
                                                                    .target_width = TR_TARGET,
                                                                    .target_height = TR_TARGET });
    atomic_store(&job->done, true);
    return nullptr;
}

static int check_empty_stream(TrCtx* c) {
    GpuDrawList list = gpu_ir_to_draw_cmds(
        c->out_arena,
        &(GpuIrInput) { .bytes = 0, .global_alpha = 255, .target_width = TR_TARGET, .target_height = TR_TARGET });
    CHECK(list.count == 0, "translate: empty stream should produce no commands");
    CHECK(list.clip_count == 1, "translate: clips[0] (the full target) exists even for an empty stream");
    return 0;
}

static int check_solid_rect(TrCtx* c) {
    c->start = ir_begin(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { 10.0f, 20.0f, 30.0f, 40.0f }, red);
    tr_run(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1, "translate: one solid rect should produce one command");
    CHECK(cmd->kind == GpuDraw_Rect, "translate: kind should be Rect");
    CHECK(rect_words_are(cmd, (FlRect) { 10.0f, 20.0f, 30.0f, 40.0f }),
          "translate: rect fields should pass through unchanged");
    const u32 packed_red = 200u | (40u << 8) | (50u << 16) | (255u << 24);
    for (int i = 0; i < 4; i++) {
        CHECK(cmd->color[i] == packed_red,
              "translate: a solid fill should pack the same RGBA8 (R in the low byte) into all four corners");
        CHECK(cmd->radius[i] == 0.0f, "translate: a square rect should carry zero radii");
    }
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 10.0f, 20.0f, 40.0f, 60.0f }),
          "translate: AABB should be rect min/max");
    return 0;
}

static int check_global_alpha_fold(TrCtx* c) {
    tr_run(c, 128);
    const u32 color0 = c->list.cmds[0].color[0];
    CHECK((color0 >> 24) == 128u, "translate: global_alpha 128 over a=255 should fold to 128");
    CHECK((color0 & 0x00FFFFFFu) == (200u | (40u << 8) | (50u << 16)), "translate: global_alpha must not touch RGB");

    tr_begin(c);
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 8.0f, 8.0f }, blue); // a = 128
    tr_run(c, 128);
    CHECK((c->list.cmds[0].color[0] >> 24) == 64u, "translate: global_alpha 128 over a=128 should round to 64");
    return 0;
}

static int check_radius_clamping(const GpuDrawCmd* clamped) {
    static const struct {
        u32 corner;
        f32 want;
        const char* msg;
    } cases[] = {
        { 0, 10.0f, "translate: an oversized radius clamps to half the smaller dimension" },
        { 1, 5.0f, "translate: clamping must not disturb in-range radii" },
        { 2, 0.0f, "translate: a negative radius clamps to 0" },
        { 3, 10.0f, "translate: a radius at exactly half the smaller dimension survives" },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        CHECK(clamped->radius[cases[i].corner] == cases[i].want, cases[i].msg);
    }
    return 0;
}

static int check_rounded_radii(TrCtx* c) {
    tr_begin(c);
    ir_fill_rect(c->ir_arena, (FlRect) { 0, 0, 100, 50 }, (FlCornerRadius) { 3.0f, 5.0f, 7.0f, 9.0f },
                 (FlColor[4]) { red, red, red, red }, nullptr);
    ir_fill_rect(c->ir_arena, (FlRect) { 0, 0, 40, 20 }, (FlCornerRadius) { 30.0f, 5.0f, -3.0f, 10.0f },
                 (FlColor[4]) { red, red, red, red }, nullptr);
    tr_run(c, 255);
    CHECK(c->list.count == 2, "translate: rounded rects should translate");
    CHECK(radii_are(&c->list.cmds[0], (FlRect) { 3.0f, 5.0f, 7.0f, 9.0f }),
          "translate: in-range per-corner radii should pass through in TL TR BL BR order");
    if (check_radius_clamping(&c->list.cmds[1]) != 0) {
        return 1;
    }
    const GpuAabb* box = &c->list.aabbs[0];
    CHECK(box->x1 == 100.0f && box->y1 == 50.0f, "translate: radii must not affect the AABB (the rect bounds it)");
    return 0;
}

static int check_gradient_corner_packing(TrCtx* c) {
    const FlColor tl = { 10, 20, 30, 255 };
    const FlColor tr = { 40, 50, 60, 128 };
    const FlColor bl = { 70, 80, 90, 64 };
    const FlColor br = { 100, 110, 120, 0 };
    tr_begin(c);
    ir_fill_rect(c->ir_arena, (FlRect) { 0, 0, 50, 50 }, (FlCornerRadius) { 0 }, (FlColor[4]) { tl, tr, bl, br },
                 nullptr);
    tr_run(c, 255);
    CHECK(c->list.count == 1, "translate: a gradient (distinct corner colors) should translate");
    const u32* color = c->list.cmds[0].color;
    CHECK(color[0] == (10u | (20u << 8) | (30u << 16) | (255u << 24))
              && color[1] == (40u | (50u << 8) | (60u << 16) | (128u << 24))
              && color[2] == (70u | (80u << 8) | (90u << 16) | (64u << 24))
              && color[3] == (100u | (110u << 8) | (120u << 16)),
          "translate: corner colors should pack per corner in TL TR BL BR order");
    tr_run(c, 128);
    const u32* folded = c->list.cmds[0].color;
    CHECK((folded[0] >> 24) == 128u && (folded[1] >> 24) == 64u && (folded[2] >> 24) == 32u && (folded[3] >> 24) == 0u,
          "translate: global_alpha should fold into every corner's alpha");
    return 0;
}

static int check_survivor_tail(const GpuDrawList* list) {
    CHECK(list->cmds[2].rect[0] == 1.0f && list->cmds[2].rect[3] == 4.0f,
          "translate: the last surviving command should be the trailing solid rect");
    CHECK(list->clip_count == 1 && list->cmds[0].clip == 0 && list->cmds[2].clip == 0,
          "translate: unclipped commands reference clips[0], and an unused push adds no clip entry");
    CHECK(aabb_is(&list->clips[0], (GpuAabb) { 0.0f, 0.0f, (f32)TR_TARGET, (f32)TR_TARGET }),
          "translate: clips[0] is the full target rect");
    return 0;
}

static int check_survivors(const GpuDrawList* list) {
    CHECK(list->count == 3, "translate: empty/non-rect records should be skipped; rounded and gradient kept");
    CHECK(list->cmds[0].radius[0] == 4.0f, "translate: the first surviving command should be the rounded rect");
    CHECK(list->cmds[1].color[3] != list->cmds[1].color[0],
          "translate: the second surviving command should be the gradient");
    return check_survivor_tail(list);
}

static int check_skipped_records(TrCtx* c) {
    tr_begin(c);
    ir_fill_rect(c->ir_arena, (FlRect) { 0, 0, 50, 50 }, (FlCornerRadius) { .tl = 4.0f },
                 (FlColor[4]) { red, red, red, red }, nullptr);
    ir_fill_rect(c->ir_arena, (FlRect) { 0, 0, 50, 50 }, (FlCornerRadius) { 0 }, (FlColor[4]) { red, red, red, blue },
                 nullptr);
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 0.0f, 50.0f }, red);          // empty: w == 0
    FlPolyPoint* pts = ir_polyline(c->ir_arena, red, red, 1.0f, 0.0f, 0, 1, nullptr); // one point: no segment
    pts[0] = (FlPolyPoint) { 0.0f, 0.0f, 1.0f };
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 50, 50 });
    ir_clip_pop(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { 1.0f, 2.0f, 3.0f, 4.0f }, red);
    tr_run(c, 255);
    return check_survivors(&c->list);
}

static void emit_clip_dedup_scene(FlArena* ir_arena) {
    const FlRect full = { 0.0f, 0.0f, 400.0f, 400.0f };
    ir_clip_push(ir_arena, (FlRect) { 100, 100, 200, 200 }); // A
    emit_solid_rect(ir_arena, full, red);                    // index 0: clipped by A
    ir_clip_push(ir_arena, (FlRect) { 150, 50, 300, 100 });  // B; effective = A ^ B = {150,100,300,150}
    emit_solid_rect(ir_arena, full, red);                    // index 1: clipped by A ^ B
    ir_clip_pop(ir_arena);
    emit_solid_rect(ir_arena, full, red); // index 2: back to A (dedups with index 0)
    ir_clip_pop(ir_arena);
    emit_solid_rect(ir_arena, full, red);                    // index 3: full target
    ir_clip_push(ir_arena, (FlRect) { 100, 100, 200, 200 }); // A re-pushed
    emit_solid_rect(ir_arena, full, red);                    // index 4: dedups with A's entry
    ir_clip_pop(ir_arena);
}

static int check_clip_entries(const GpuDrawList* list) {
    CHECK(list->count == 5, "clip: every partially visible command translates");
    CHECK(list->clip_count == 3, "clip: dedup keeps one entry per distinct effective rect");
    CHECK(list->cmds[0].clip == 1 && aabb_is(&list->clips[1], (GpuAabb) { 100.0f, 100.0f, 300.0f, 300.0f }),
          "clip: a pushed rect becomes the command's clip entry");
    CHECK(list->cmds[1].clip == 2 && aabb_is(&list->clips[2], (GpuAabb) { 150.0f, 100.0f, 300.0f, 150.0f }),
          "clip: a nested push intersects with the enclosing clip");
    return 0;
}

static int check_clip_references(const GpuDrawList* list) {
    CHECK(list->cmds[2].clip == 1, "clip: a pop restores the enclosing clip, reusing its entry");
    CHECK(list->cmds[3].clip == 0, "clip: popping the last level restores the full target");
    CHECK(list->cmds[4].clip == 1, "clip: re-pushing an identical rect dedups to the existing entry");
    return 0;
}

static int check_clip_aabbs(const GpuDrawList* list) {
    CHECK(aabb_is(&list->aabbs[0], (GpuAabb) { 100.0f, 100.0f, 300.0f, 300.0f }),
          "clip: the AABB is pre-intersected with the clip rect");
    CHECK(list->aabbs[1].x0 == 150.0f && list->aabbs[1].y1 == 150.0f,
          "clip: a nested command's AABB intersects the whole stack");
    CHECK(list->cmds[0].rect[0] == 0.0f && list->cmds[0].rect[2] == 400.0f,
          "clip: the command's rect passes through unclipped (the raster pass cuts coverage)");
    return 0;
}

static int check_clip_dedup(TrCtx* c) {
    tr_begin(c);
    emit_clip_dedup_scene(c->ir_arena);
    tr_run(c, 255);
    if (check_clip_entries(&c->list) != 0) {
        return 1;
    }
    if (check_clip_references(&c->list) != 0) {
        return 1;
    }
    return check_clip_aabbs(&c->list);
}

static int check_clip_dropping(TrCtx* c) {
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 50, 50 });
    emit_solid_rect(c->ir_arena, (FlRect) { 100.0f, 100.0f, 20.0f, 20.0f }, red); // outside the clip: dropped
    emit_solid_rect(c->ir_arena, (FlRect) { 40.0f, 40.0f, 20.0f, 20.0f }, red);   // partial overlap: survives
    ir_clip_push(c->ir_arena, (FlRect) { 60, 60, 10, 10 });                       // disjoint from the outer clip: empty
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 100.0f, 100.0f }, red);   // under an empty clip: dropped
    ir_clip_pop(c->ir_arena);
    ir_clip_pop(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { (f32)TR_TARGET + 10.0f, 0.0f, 20.0f, 20.0f }, red); // off-target: dropped
    tr_run(c, 255);
    CHECK(c->list.count == 1, "clip: fully clipped-out and off-target commands never translate");
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 40.0f, 40.0f, 50.0f, 50.0f }),
          "clip: the surviving command's AABB clamps to the clip");
    return 0;
}

static int check_unbalanced_clips(TrCtx* c) {
    tr_begin(c);
    ir_clip_pop(c->ir_arena); // nothing to pop
    emit_solid_rect(c->ir_arena, (FlRect) { 10.0f, 10.0f, 20.0f, 20.0f }, red);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 30, 30 }); // never popped
    emit_solid_rect(c->ir_arena, (FlRect) { 10.0f, 10.0f, 40.0f, 40.0f }, red);
    tr_run(c, 255);
    CHECK(c->list.count == 2, "clip: an unbalanced stream still translates");
    CHECK(c->list.cmds[0].clip == 0, "clip: a pop on an empty stack is ignored");
    CHECK(c->list.cmds[1].clip == 1 && c->list.aabbs[1].x1 == 30.0f && c->list.aabbs[1].y1 == 30.0f,
          "clip: an unclosed push still clips the commands after it");
    return 0;
}

static int check_deep_clip_stack(TrCtx* c) {
    tr_begin(c);
    for (int d = 0; d < 40; d++) {
        ir_clip_push(c->ir_arena, (FlRect) { (f32)d, (f32)d, (f32)TR_TARGET, (f32)TR_TARGET });
    }
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 100.0f, 100.0f }, red);
    for (int d = 0; d < 40; d++) {
        ir_clip_pop(c->ir_arena);
    }
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 100.0f, 100.0f }, red);
    tr_run(c, 255);
    CHECK(c->list.count == 2, "clip: a stack deeper than capacity still translates");
    CHECK(c->list.aabbs[0].x0 == 31.0f && c->list.aabbs[0].y0 == 31.0f,
          "clip: pushes past capacity are ignored, keeping the deepest tracked intersection");
    CHECK(c->list.cmds[1].clip == 0, "clip: pops unwind an overflowed stack back to the full target");
    return 0;
}

static int check_image_translation(TrCtx* c) {
    // A GpuDraw_Image command's radius words carry the source rect in atlas texel coordinates and its
    // color[0] is the premultiplied tint.
    gpu_image_atlas_init(&c->atlas, c->out_arena);
    const GpuImageAtlasEntry* img_a = gpu_image_atlas_place(&c->atlas, 7, 8, 8);
    CHECK(img_a != nullptr, "image translate: test placement must succeed");
    c->atlas_x = (f32)img_a->cell.x;
    c->atlas_y = (f32)img_a->cell.y;
    const FlImageSource src_a = { .kind = FlImageKind_Static, .handle = 7 };

    tr_begin(c);
    FlArena* a = c->ir_arena;
    ir_image(a, src_a, FlImageMode_Color, (FlRect) { 10, 20, 8, 8 }, (FlRect) { 0 }, white, FlImageFilter_Linear,
             nullptr); // whole image, linear
    ir_image(a, src_a, FlImageMode_Color, (FlRect) { 30, 20, 4, 3 }, (FlRect) { 2, 1, 4, 3 }, white,
             FlImageFilter_Nearest, nullptr); // sub-rect, nearest
    ir_image(a, src_a, FlImageMode_Color, (FlRect) { 50, 20, 8, 8 }, (FlRect) { 6, 6, 8, 8 }, white,
             FlImageFilter_Linear, nullptr); // overhanging src
    ir_image(a, (FlImageSource) { .kind = FlImageKind_Dynamic }, FlImageMode_Color, (FlRect) { 70, 20, 8, 8 },
             (FlRect) { 0 }, white, FlImageFilter_Linear,
             nullptr); // dynamic source: skipped without slot bookkeeping (slots == nullptr here)
    ir_image(a, (FlImageSource) { .kind = FlImageKind_Static, .handle = 99 }, FlImageMode_Color,
             (FlRect) { 90, 20, 8, 8 }, (FlRect) { 0 }, white, FlImageFilter_Linear,
             nullptr); // unresolved handle: skipped
    tr_run_atlas(c, 255);
    CHECK(c->list.count == 3, "image translate: resolved static images translate; dynamic and unknown are skipped");
    return 0;
}

static int check_image_whole(const GpuDrawList* list, f32 ax, f32 ay) {
    const GpuDrawCmd* whole = &list->cmds[0];
    CHECK(whole->kind == GpuDraw_Image, "image translate: kind is Image");
    CHECK(whole->rect[0] == 10.0f && whole->rect[2] == 8.0f, "image translate: dst passes through");
    CHECK(radii_are(whole, (FlRect) { ax, ay, ax + 8.0f, ay + 8.0f }),
          "image translate: an empty src rect maps to the whole cell in atlas texels");
    return 0;
}

static int check_image_whole_flags(const GpuDrawList* list) {
    const GpuDrawCmd* whole = &list->cmds[0];
    CHECK(whole->color[0] == 0xFFFFFFFFu, "image translate: the opaque white tint is the identity");
    CHECK(whole->flags == 0, "image translate: linear filtering carries no flag");
    CHECK(list->aabbs[0].x0 == 10.0f && list->aabbs[0].x1 == 18.0f, "image translate: the AABB is the dst rect");
    return 0;
}

static int check_image_sub_rects(const GpuDrawList* list, f32 ax, f32 ay) {
    const GpuDrawCmd* sub = &list->cmds[1];
    CHECK(radii_are(sub, (FlRect) { ax + 2.0f, ay + 1.0f, ax + 6.0f, ay + 4.0f }),
          "image translate: a src sub-rect maps into the cell");
    CHECK(sub->flags == GPU_DRAW_IMAGE_NEAREST, "image translate: nearest filtering sets the flag");
    const GpuDrawCmd* over = &list->cmds[2];
    CHECK(over->radius[0] == ax + 6.0f && over->radius[2] == ax + 8.0f,
          "image translate: a src rect overhanging the image clamps to its extent");
    return 0;
}

static int check_image_fields(TrCtx* c) {
    if (check_image_whole(&c->list, c->atlas_x, c->atlas_y) != 0) {
        return 1;
    }
    if (check_image_whole_flags(&c->list) != 0) {
        return 1;
    }
    return check_image_sub_rects(&c->list, c->atlas_x, c->atlas_y);
}

static int check_image_tint(TrCtx* c) {
    const FlImageSource src_a = { .kind = FlImageKind_Static, .handle = 7 };
    tr_begin(c);
    ir_image(c->ir_arena, src_a, FlImageMode_Color, (FlRect) { 10, 20, 8, 8 }, (FlRect) { 0 },
             (FlColor) { 200, 100, 50, 128 }, FlImageFilter_Linear, nullptr);
    tr_run_atlas(c, 255);
    CHECK(c->list.cmds[0].color[0] == (100u | (50u << 8) | (25u << 16) | (128u << 24)),
          "image translate: the tint packs premultiplied (rgb * a, round-to-nearest)");
    tr_run_atlas(c, 128);
    const u32 tint = c->list.cmds[0].color[0];
    CHECK((tint >> 24) == 64u && (tint & 0xFFu) == 50u,
          "image translate: global_alpha folds into the premultiplied tint");
    return 0;
}

static int check_image_clip_and_release(TrCtx* c) {
    const FlImageSource src_a = { .kind = FlImageKind_Static, .handle = 7 };
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 14, 24 });
    ir_image(c->ir_arena, src_a, FlImageMode_Color, (FlRect) { 10, 20, 8, 8 }, (FlRect) { 0 }, white,
             FlImageFilter_Linear, nullptr); // partial overlap
    ir_image(c->ir_arena, src_a, FlImageMode_Color, (FlRect) { 100, 20, 8, 8 }, (FlRect) { 0 }, white,
             FlImageFilter_Linear, nullptr); // fully clipped out
    ir_clip_pop(c->ir_arena);
    tr_run_atlas(c, 255);
    CHECK(c->list.count == 1, "image translate: a fully clipped image never translates");
    CHECK(c->list.aabbs[0].x1 == 14.0f && c->list.aabbs[0].y1 == 24.0f,
          "image translate: the image AABB pre-intersects with its clip");
    CHECK(c->list.cmds[0].clip == 1, "image translate: the image carries its clip index");
    gpu_image_atlas_release(&c->atlas, 7);
    tr_run_atlas(c, 255);
    CHECK(c->list.count == 0, "image translate: a released handle stops translating");
    return 0;
}

static int check_translate_sweep(TrCtx* c) {
    tr_begin(c);
    u32 rng = 0x12345678u;
    enum { SWEEP_COUNT = 100 };
    f32 expected[SWEEP_COUNT][4];
    for (u32 i = 0; i < SWEEP_COUNT; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        expected[i][0] = (f32)(rng & 1023u);
        expected[i][1] = (f32)((rng >> 10) & 1023u);
        expected[i][2] = (f32)(1u + ((rng >> 20) & 255u));
        expected[i][3] = (f32)(1u + ((rng >> 24) & 127u));
        emit_solid_rect(c->ir_arena, (FlRect) { expected[i][0], expected[i][1], expected[i][2], expected[i][3] },
                        (FlColor) { (u8)rng, (u8)(rng >> 8), (u8)(rng >> 16), 255 });
    }
    tr_run(c, 255);
    CHECK(c->list.count == SWEEP_COUNT, "translate sweep: every solid rect should translate");
    for (u32 i = 0; i < SWEEP_COUNT; i++) {
        const f32* e = expected[i];
        CHECK(rect_words_are(&c->list.cmds[i], (FlRect) { e[0], e[1], e[2], e[3] }),
              "translate sweep: command order/fields should match emission order");
        CHECK(aabb_is(&c->list.aabbs[i], (GpuAabb) { e[0], e[1], e[0] + e[2], e[1] + e[3] }),
              "translate sweep: AABBs should stay parallel to commands");
    }
    return 0;
}

static int check_malformed_guard(TrCtx* c) {
    // A zero-byte_size header must break both IR walks (the draw-bound prescan and the translation pass)
    // instead of spinning forever.
    tr_begin(c);
    FlArena* ir_arena = c->ir_arena;
    emit_solid_rect(ir_arena, (FlRect) { 1.0f, 2.0f, 3.0f, 4.0f }, red);
    FlCmdHeader* bad = ir_header(ir_arena, FlCmd_FillRect, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdFillRect)));
    arena_alloc_zero(ir_arena, FlCmdFillRect); // payload present but never reached past the guard
    bad->byte_size = 0;
    emit_solid_rect(ir_arena, (FlRect) { 5.0f, 6.0f, 7.0f, 8.0f }, red); // after the corruption: must be dropped
    MalformedTranslateJob job
        = { .out_arena = c->out_arena, .commands = c->start, .bytes = ir_bytes(ir_arena, c->start) };
    Thread worker;
    CHECK(os_thread_create(&worker, malformed_translate_thread, &job), "malformed guard: worker thread must start");
    bool finished = false;
    for (int i = 0; i < 5000 && !finished; i++) { // ~5s bound; the passing path finishes on the first poll
        finished = atomic_load(&job.done);
        if (!finished) {
            sleep_ms(1);
        }
    }
    CHECK(finished, "malformed guard: a zero-byte_size header must terminate the IR walks, not hang them");
    os_thread_join(&worker);
    CHECK(job.list.count == 1, "malformed guard: records ahead of the zero-size header still translate");
    CHECK(job.list.cmds[0].rect[0] == 1.0f && job.list.cmds[0].rect[1] == 2.0f,
          "malformed guard: the surviving record is the one before the corruption");
    return 0;
}

static int run_basic_translation_checks(TrCtx* c) {
    if (check_empty_stream(c) != 0) {
        return 1;
    }
    if (check_solid_rect(c) != 0) {
        return 1;
    }
    if (check_global_alpha_fold(c) != 0) {
        return 1;
    }
    if (check_rounded_radii(c) != 0) {
        return 1;
    }
    if (check_gradient_corner_packing(c) != 0) {
        return 1;
    }
    return check_skipped_records(c);
}

static int run_clip_translation_checks(TrCtx* c) {
    if (check_clip_dedup(c) != 0) {
        return 1;
    }
    if (check_clip_dropping(c) != 0) {
        return 1;
    }
    if (check_unbalanced_clips(c) != 0) {
        return 1;
    }
    return check_deep_clip_stack(c);
}

static int run_image_translation_checks(TrCtx* c) {
    if (check_image_translation(c) != 0) {
        return 1;
    }
    if (check_image_fields(c) != 0) {
        return 1;
    }
    if (check_image_tint(c) != 0) {
        return 1;
    }
    return check_image_clip_and_release(c);
}

int run_translator_tests(void) {
    TrCtx c = { .ir_arena = arena_new(), .out_arena = arena_new() };

    if (run_basic_translation_checks(&c) != 0) {
        return 1;
    }
    if (run_clip_translation_checks(&c) != 0) {
        return 1;
    }
    if (run_image_translation_checks(&c) != 0) {
        return 1;
    }
    if (check_translate_sweep(&c) != 0) {
        return 1;
    }
    if (check_malformed_guard(&c) != 0) {
        return 1;
    }

    fl_arena_destroy(c.out_arena);
    fl_arena_destroy(c.ir_arena);
    printf("gpu_smoke_test: translator unit tests PASS\n");
    return 0;
}

// Effect annotations: a record's optional FlEffect expands the classification AABB to cover where the effect
// reaches and packs the effect parameters into every command the record commits. It is decorative only: it never
// changes which command survives beyond reaching more tiles. Still Section 1: pure CPU, no GL.

static int check_no_effect(TrCtx* c) {
    c->start = ir_begin(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { 100.0f, 100.0f, 40.0f, 40.0f }, red);
    tr_run(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1, "effect: a plain rect translates");
    CHECK(effect_kind_of(cmd) == 0, "effect: a rect with no effect carries no effect kind");
    const bool words_zero = cmd->effect[0] == 0 && cmd->effect[1] == 0 && cmd->effect[2] == 0 && cmd->effect[3] == 0;
    CHECK(words_zero, "effect: a rect with no effect leaves the effect words zero");
    const GpuAabb* box = &c->list.aabbs[0];
    CHECK(aabb_is(box, (GpuAabb) { 100.0f, 100.0f, 140.0f, 140.0f }),
          "effect: a rect with no effect bounds exactly the rect");
    return 0;
}

static void emit_effect_rect(TrCtx* c, const FlEffect* fx) {
    tr_begin(c);
    emit_rect_effect(c->ir_arena, (FlRect) { 100.0f, 100.0f, 40.0f, 40.0f }, red, fx);
}

static int check_drop_shadow(TrCtx* c, const FlEffect* shadow) {
    emit_effect_rect(c, shadow);
    tr_run(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1, "shadow: the shadowed rect translates");
    CHECK(effect_kind_of(cmd) == 1, "shadow: the flags carry effect kind 1 (drop shadow)");
    const bool words_roundtrip
        = bits_f32(cmd->effect[0]) == 20.0f && bits_f32(cmd->effect[1]) == 10.0f && bits_f32(cmd->effect[2]) == 5.0f;
    CHECK(words_roundtrip, "shadow: offset and blur round-trip into the effect words");
    const u32 packed = packed_rgba(shadow->color, 255);
    CHECK(cmd->effect[3] == packed, "shadow: the effect color packs like any other color");
    // rect [100,100,140,140]; shadow reach = [115,105,165,155]; union = [100,100,165,155].
    const GpuAabb* box = &c->list.aabbs[0];
    CHECK(aabb_is(box, (GpuAabb) { 100.0f, 100.0f, 165.0f, 155.0f }),
          "shadow: the AABB unions the rect with its offset+blur reach");
    return 0;
}

static int check_glow_and_none(TrCtx* c, FlColor shadow_color) {
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = shadow_color, .off_x = 20.0f, .off_y = 10.0f, .blur = 8.0f };
    emit_effect_rect(c, &glow);
    tr_run(c, 255);
    CHECK(effect_kind_of(&c->list.cmds[0]) == 2, "glow: the flags carry effect kind 2 (glow)");
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 92.0f, 92.0f, 148.0f, 148.0f }),
          "glow: the AABB dilates the rect by the blur, ignoring the offset");

    FlEffect none
        = { .kind = FlEffectKind_None, .color = { 1, 2, 3, 4 }, .off_x = 50.0f, .off_y = 50.0f, .blur = 9.0f };
    emit_effect_rect(c, &none);
    tr_run(c, 255);
    CHECK(effect_kind_of(&c->list.cmds[0]) == 0 && c->list.cmds[0].effect[3] == 0,
          "effect: a kind-0 (none) effect is ignored even with the flag set");
    CHECK(c->list.aabbs[0].x1 == 140.0f && c->list.aabbs[0].y1 == 140.0f,
          "effect: a kind-0 (none) effect expands nothing");
    return 0;
}

static int check_shadow_variants(TrCtx* c, const FlEffect* shadow) {
    FlEffect sharp
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 6.0f, .off_y = -4.0f, .blur = -5.0f };
    emit_effect_rect(c, &sharp);
    tr_run(c, 255);
    CHECK(bits_f32(c->list.cmds[0].effect[2]) == 0.0f, "shadow: a negative blur clamps to zero in the words");
    // reach = [106,96,146,136]; union with [100,100,140,140] = [100,96,146,140].
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 100.0f, 96.0f, 146.0f, 140.0f }),
          "shadow: a clamped blur shifts the reach by the offset only");

    emit_effect_rect(c, shadow);
    tr_run(c, 128);
    CHECK((c->list.cmds[0].effect[3] >> 24) == ((255u * 128u + 127u) / 255u),
          "shadow: global_alpha folds into the effect color's alpha");
    return 0;
}

static int check_shadow_reach_into_clip(TrCtx* c) {
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 200, 100, 100, 100 }); // clip [200,100,300,200]
    FlEffect reach
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 80.0f, .off_y = 0.0f, .blur = 4.0f };
    emit_rect_effect(c->ir_arena, (FlRect) { 130.0f, 120.0f, 20.0f, 20.0f }, red,
                     &reach); // body [130,120,150,140], left of clip
    ir_clip_pop(c->ir_arena);
    tr_run(c, 255);
    CHECK(c->list.count == 1, "shadow: a body outside the clip still survives when its shadow reaches in");
    // union(body, reach) = union([130,120,150,140], [206,116,234,144]) = [130,116,234,144];
    // intersect clip [200,100,300,200] = [200,116,234,144].
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 200.0f, 116.0f, 234.0f, 144.0f }),
          "shadow: the surviving AABB is the shadow reach intersected with the clip");
    return 0;
}

static int check_bar_glow(TrCtx* c) {
    tr_begin(c);
    const FlColor bar_color = { 10, 20, 30, 255 };
    FlEffect bar_glow = { .kind = FlEffectKind_Glow, .color = bar_color, .blur = 3.0f };
    FlBar* bars = ir_bars(c->ir_arena, 200.0f, red, 3, &bar_glow);
    bars[0] = (FlBar) { 10.0f, 8.0f, 20.0f };
    bars[1] = (FlBar) { 40.0f, 8.0f, 30.0f };
    bars[2] = (FlBar) { 70.0f, 8.0f, 40.0f };
    tr_run(c, 255);
    CHECK(c->list.count == 3, "bar glow: every bar of the set translates");
    for (u32 i = 0; i < 3; i++) {
        CHECK(effect_kind_of(&c->list.cmds[i]) == 2 && c->list.cmds[i].effect[3] == packed_rgba(bar_color, 255),
              "bar glow: the one effect stamps onto every bar the set commits");
    }
    return 0;
}

static int check_shape_effect_kinds(TrCtx* c, u32 fx_packed, FlColor fx_color) {
    // record_effect must locate the trailing FlEffect for every kind the translator renders; a wrong per-kind
    // payload size would mislocate it. The round-tripped color word proves it was found at the right offset.
    tr_begin(c);
    FlEffect arc_fx
        = { .kind = FlEffectKind_DropShadow, .color = fx_color, .off_x = 4.0f, .off_y = 0.0f, .blur = 2.0f };
    ir_arc(c->ir_arena, 200.0f, 200.0f, 40.0f, 8.0f, 0.0f, 0.25f * TAU, red, 0, &arc_fx);
    FlEffect circle_fx = { .kind = FlEffectKind_Glow, .color = fx_color, .blur = 3.0f };
    ir_circle(c->ir_arena, 200.0f, 200.0f, 20.0f, red, red, 0.0f, &circle_fx);
    FlEffect diamond_fx
        = { .kind = FlEffectKind_DropShadow, .color = fx_color, .off_x = 5.0f, .off_y = 5.0f, .blur = 1.0f };
    ir_diamond(c->ir_arena, 200.0f, 200.0f, 15.0f, 15.0f, red, red, 0.0f, &diamond_fx);
    tr_run(c, 255);
    CHECK(c->list.count == 3, "effect kinds: arc, circle, and diamond each translate");
    const GpuDrawCmd* arc = &c->list.cmds[0];
    CHECK(effect_kind_of(arc) == 1 && arc->effect[3] == fx_packed,
          "effect kinds: an arc's effect is located past its payload and stamped");
    const GpuDrawCmd* circle = &c->list.cmds[1];
    CHECK(effect_kind_of(circle) == 2 && circle->effect[3] == fx_packed,
          "effect kinds: a circle's effect is located past its payload and stamped");
    const GpuDrawCmd* diamond = &c->list.cmds[2];
    CHECK(effect_kind_of(diamond) == 1 && diamond->effect[3] == fx_packed,
          "effect kinds: a diamond's effect is located past its payload and stamped");
    return 0;
}

static int check_border_effect_kind(TrCtx* c, u32 fx_packed, FlColor fx_color) {
    // Border is core-tier (its builder has no effect parameter); assemble the record directly with the effect
    // at the tail, as the painter's annotation path would.
    tr_begin(c);
    FlEffect border_fx
        = { .kind = FlEffectKind_DropShadow, .color = fx_color, .off_x = 3.0f, .off_y = 3.0f, .blur = 2.0f };
    FlCmdHeader* bheader = ir_header(c->ir_arena, FlCmd_Border,
                                     (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdBorder)) + ir_effect_bytes(&border_fx));
    FlCmdBorder* bd = arena_alloc_zero(c->ir_arena, FlCmdBorder);
    bd->rect = (FlRect) { 100, 100, 60, 40 };
    bd->w_l = 4.0f;
    bd->w_r = 4.0f;
    bd->w_t = 4.0f;
    bd->w_b = 4.0f;
    bd->color = red;
    ir_append_effect(c->ir_arena, bheader, &border_fx);
    tr_run(c, 255);
    CHECK(c->list.count == 1 && c->list.cmds[0].kind == GpuDraw_Border, "effect kinds: the border translates");
    CHECK(effect_kind_of(&c->list.cmds[0]) == 1 && c->list.cmds[0].effect[3] == fx_packed,
          "effect kinds: a border's effect is located past its payload and stamped");
    return 0;
}

static int check_image_effect_kind(TrCtx* c, u32 fx_packed, FlColor fx_color) {
    // Image is core-tier too, and needs an atlas entry to translate.
    gpu_image_atlas_init(&c->atlas, c->out_arena);
    gpu_image_atlas_place(&c->atlas, 42, 16, 16);
    tr_begin(c);
    FlEffect image_fx = { .kind = FlEffectKind_Glow, .color = fx_color, .blur = 4.0f };
    FlCmdHeader* iheader = ir_header(c->ir_arena, FlCmd_Image,
                                     (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdImage)) + ir_effect_bytes(&image_fx));
    FlCmdImage* im = arena_alloc_zero(c->ir_arena, FlCmdImage);
    im->source = (FlImageSource) { .kind = FlImageKind_Static, .handle = 42 };
    im->dst = (FlRect) { 100, 100, 16, 16 };
    im->tint = white;
    ir_append_effect(c->ir_arena, iheader, &image_fx);
    tr_run_atlas(c, 255);
    CHECK(c->list.count == 1 && c->list.cmds[0].kind == GpuDraw_Image, "effect kinds: the atlas image translates");
    CHECK(effect_kind_of(&c->list.cmds[0]) == 2 && c->list.cmds[0].effect[3] == fx_packed,
          "effect kinds: an image's effect is located past its payload and stamped");
    return 0;
}

static int run_effect_shape_checks(TrCtx* c) {
    const FlColor fx_color = { 12, 34, 56, 255 };
    const u32 fx_packed = packed_rgba(fx_color, 255);
    if (check_shape_effect_kinds(c, fx_packed, fx_color) != 0) {
        return 1;
    }
    if (check_border_effect_kind(c, fx_packed, fx_color) != 0) {
        return 1;
    }
    return check_image_effect_kind(c, fx_packed, fx_color);
}

int run_effect_translator_tests(void) {
    TrCtx c = { .ir_arena = arena_new(), .out_arena = arena_new() };
    const FlColor shadow_color = { 30, 220, 40, 255 };
    FlEffect shadow
        = { .kind = FlEffectKind_DropShadow, .color = shadow_color, .off_x = 20.0f, .off_y = 10.0f, .blur = 5.0f };

    if (check_no_effect(&c) != 0) {
        return 1;
    }
    if (check_drop_shadow(&c, &shadow) != 0) {
        return 1;
    }
    if (check_glow_and_none(&c, shadow_color) != 0) {
        return 1;
    }
    if (check_shadow_variants(&c, &shadow) != 0) {
        return 1;
    }
    if (check_shadow_reach_into_clip(&c) != 0) {
        return 1;
    }
    if (check_bar_glow(&c) != 0) {
        return 1;
    }
    if (run_effect_shape_checks(&c) != 0) {
        return 1;
    }

    fl_arena_destroy(c.out_arena);
    fl_arena_destroy(c.ir_arena);
    printf("gpu_smoke_test: effect translator unit tests PASS\n");
    return 0;
}

#endif // !defined(__APPLE__)
