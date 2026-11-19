// GPU smoke test: sampler-slot registry and slot, background, text and SVG translator unit tests.

#include "gpu_test_translate.h"

#if !defined(__APPLE__)

// Sampler-slot bookkeeping unit tests (no GL; pure CPU): the gpu_image_slots registry and the per-frame
// slot table.

static u32 count_dynamic_entries(const GpuImageSlots* slots) {
    u32 n = 0;
    for (u32 i = 0; i < slots->image_count; i++) {
        n += slots->images[i].is_dynamic ? 1u : 0u;
    }
    return n;
}

typedef struct SlotsCtx {
    FlArena* arena;
    GpuImageSlots slots;
    int big;
    int da;
    int db;
    const u32* dead;
} SlotsCtx;

static u8 slot_buf_a[16 * 16 * 4];
static u8 slot_buf_b[16];

static int check_slots_static_register(SlotsCtx* s) {
    gpu_image_slots_init(&s->slots, s->arena);
    gpu_image_slots_begin_frame(&s->slots);

    CHECK(gpu_image_slots_find_static(&s->slots, 7) < 0, "slots: an unknown handle does not resolve");
    s->big = gpu_image_slots_register_static(&s->slots, 7, 300, 40, 11);
    CHECK(s->big >= 0, "slots: a large image registers");
    CHECK(gpu_image_slots_find_static(&s->slots, 7) == s->big, "slots: the handle resolves to its entry");
    return 0;
}

static int check_slots_static_entry(SlotsCtx* s) {
    const GpuSlotImage* entry = &s->slots.images[s->big];
    CHECK(entry->w == 300 && entry->h == 40 && entry->tex == 11 && !entry->is_dynamic,
          "slots: the entry keeps the image extent and texture");
    CHECK(gpu_image_slots_register_static(&s->slots, 7, 8, 8, 12) < 0, "slots: a handle cannot register twice");
    return 0;
}

static int check_slots_dynamic_create(SlotsCtx* s) {
    // Dynamic entries key on the buffer pointer, so re-touching resolves to the same entry, which is what
    // keeps the translator's two passes consistent.
    s->da = gpu_image_slots_touch_dynamic(&s->slots, (GpuDynamicImage) { slot_buf_a, 4, 4, 16, false });
    CHECK(s->da >= 0, "slots: a dynamic buffer creates an entry");
    const int again = gpu_image_slots_touch_dynamic(&s->slots, (GpuDynamicImage) { slot_buf_a, 4, 4, 16, false });
    CHECK(again == s->da, "slots: re-touching the same buffer resolves to the same entry");
    const GpuSlotImage* entry = &s->slots.images[s->da];
    CHECK(entry->is_dynamic && entry->dirty && entry->last_ref == s->slots.frame,
          "slots: a touch marks the entry dirty and referenced this frame");
    return 0;
}

static int check_slots_dynamic_second(SlotsCtx* s) {
    s->db = gpu_image_slots_touch_dynamic(&s->slots, (GpuDynamicImage) { slot_buf_b, 2, 2, 8, true });
    CHECK(s->db >= 0 && s->db != s->da, "slots: distinct buffers get distinct entries");
    CHECK(s->slots.images[s->db].alpha_only, "slots: the entry keeps the alpha-coverage mode");
    return 0;
}

static int check_slots_dims_change(SlotsCtx* s) {
    s->slots.images[s->da].tex = 55; // as if the renderer had realized it
    const int touched = gpu_image_slots_touch_dynamic(&s->slots, (GpuDynamicImage) { slot_buf_a, 8, 8, 32, false });
    CHECK(touched == s->da, "slots: a dims change keeps the entry");
    const GpuSlotImage* entry = &s->slots.images[s->da];
    CHECK(entry->tex == 0 && entry->w == 8 && entry->h == 8, "slots: a dims change resets the texture for recreation");
    CHECK(gpu_image_slots_take_dead(&s->slots, &s->dead) == 1 && s->dead[0] == 55,
          "slots: the replaced texture joins the dead list");
    CHECK(gpu_image_slots_take_dead(&s->slots, &s->dead) == 0, "slots: take_dead drains the list");
    return 0;
}

static int check_slots_assign(SlotsCtx* s) {
    gpu_image_slots_frame_reset(&s->slots);
    CHECK(gpu_image_slots_assign(&s->slots, (u32)s->big, 1) == 0
              && gpu_image_slots_assign(&s->slots, (u32)s->da, 0) == 1,
          "slots: slots assign in first-reference order");
    CHECK(gpu_image_slots_assign(&s->slots, (u32)s->big, 0) == 0, "slots: a repeat reference reuses its slot");
    const GpuImageSlots* sl = &s->slots;
    CHECK(sl->slot_count == 2 && sl->slot_image[0] == (u32)s->big && sl->slot_image[1] == (u32)s->da,
          "slots: the frame table records the assignments");
    CHECK(sl->slot_filter[0] == 1, "slots: the first reference's filter stands");
    return 0;
}

static int check_slots_overflow_register(SlotsCtx* s) {
    gpu_image_slots_frame_reset(&s->slots);
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT + 2; i++) {
        CHECK(gpu_image_slots_register_static(&s->slots, 100 + i, 300, 8, 20 + i) >= 0,
              "slots overflow: the registry holds more entries than the per-frame slots");
    }
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT; i++) {
        const u32 entry = (u32)gpu_image_slots_find_static(&s->slots, 100 + i);
        CHECK(gpu_image_slots_assign(&s->slots, entry, 0) == (int)i,
              "slots overflow: the first GPU_IMAGE_SLOT_COUNT images claim slots in order");
    }
    return 0;
}

static int check_slots_overflow_refusal(SlotsCtx* s) {
    const u32 first_refused = (u32)gpu_image_slots_find_static(&s->slots, 100 + GPU_IMAGE_SLOT_COUNT);
    CHECK(gpu_image_slots_assign(&s->slots, first_refused, 0) < 0, "slots overflow: the next image is refused");
    CHECK(s->slots.overflow == 1, "slots overflow: the counter counts the refusal");
    const u32 second_refused = (u32)gpu_image_slots_find_static(&s->slots, 101 + GPU_IMAGE_SLOT_COUNT);
    CHECK(gpu_image_slots_assign(&s->slots, second_refused, 0) < 0 && s->slots.overflow == 2,
          "slots overflow: every refused command counts");
    CHECK(gpu_image_slots_assign(&s->slots, (u32)s->slots.slot_image[0], 0) == 0,
          "slots overflow: already-slotted images keep resolving");
    gpu_image_slots_frame_reset(&s->slots);
    CHECK(s->slots.slot_count == 0 && s->slots.overflow == 0, "slots: frame reset clears the table and counter");
    return 0;
}

static int check_slots_release(SlotsCtx* s) {
    gpu_image_slots_release_static(&s->slots, 7);
    CHECK(gpu_image_slots_find_static(&s->slots, 7) < 0, "slots release: the handle stops resolving");
    gpu_image_slots_release_static(&s->slots, 7);
    CHECK(gpu_image_slots_take_dead(&s->slots, &s->dead) == 1 && s->dead[0] == 11,
          "slots release: the texture joins the dead list once");
    return 0;
}

static int check_slots_eviction(SlotsCtx* s) {
    u32 dynamics = 0;
    for (u32 i = 0; i < s->slots.image_count; i++) {
        if (s->slots.images[i].is_dynamic) {
            s->slots.images[i].tex = 70 + dynamics; // as if realized, so eviction has textures to retire
            dynamics++;
        }
    }
    CHECK(dynamics == 2, "slots eviction: both dynamic entries are live before aging");
    for (u32 i = 0; i <= GPU_IMAGE_DYNAMIC_EVICT_FRAMES; i++) {
        gpu_image_slots_begin_frame(&s->slots);
    }
    CHECK(count_dynamic_entries(&s->slots) == 0, "slots eviction: stale dynamic entries evict at frame start");
    CHECK(gpu_image_slots_find_static(&s->slots, 100) >= 0, "slots eviction: statics never evict");
    CHECK(gpu_image_slots_take_dead(&s->slots, &s->dead) == 2, "slots eviction: evicted textures join the dead list");
    return 0;
}

static int run_slots_registry_checks(SlotsCtx* s) {
    if (check_slots_static_register(s) != 0) {
        return 1;
    }
    if (check_slots_static_entry(s) != 0) {
        return 1;
    }
    if (check_slots_dynamic_create(s) != 0) {
        return 1;
    }
    if (check_slots_dynamic_second(s) != 0) {
        return 1;
    }
    return check_slots_dims_change(s);
}

static int run_slots_frame_checks(SlotsCtx* s) {
    if (check_slots_assign(s) != 0) {
        return 1;
    }
    if (check_slots_overflow_register(s) != 0) {
        return 1;
    }
    if (check_slots_overflow_refusal(s) != 0) {
        return 1;
    }
    if (check_slots_release(s) != 0) {
        return 1;
    }
    return check_slots_eviction(s);
}

int run_slots_tests(void) {
    SlotsCtx s = { .arena = arena_new() };
    if (run_slots_registry_checks(&s) != 0) {
        return 1;
    }
    if (run_slots_frame_checks(&s) != 0) {
        return 1;
    }

    fl_arena_destroy(s.arena);
    printf("gpu_smoke_test: image-slot unit tests PASS\n");
    return 0;
}

// Slot-image translation (no GL): large statics and dynamic buffers become GpuDraw_SlotImage commands with
// per-frame slot indices in aux.

static u8 dyn_buf[4 * 4 * 4];
static const FlImageSource src_small = { .kind = FlImageKind_Static, .handle = 3 };
static const FlImageSource src_large = { .kind = FlImageKind_Static, .handle = 7 };
static const FlImageSource src_dyn
    = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_buf, .w = 4, .h = 4, .stride = 16 } };

static void emit_image_at(FlArena* arena, FlImageSource src, FlRect dst, FlImageFilter filter) {
    ir_image(arena, src, FlImageMode_Color, dst, (FlRect) { 0 }, white, filter, nullptr);
}

static void tr_run_slots(TrCtx* c, u8 global_alpha) {
    GpuIrInput in = tr_input(c, global_alpha);
    in.atlas = &c->atlas;
    in.slots = &c->slots;
    tr_run_input(c, &in);
}

static void tr_run_atlas_only(TrCtx* c, u8 global_alpha) {
    GpuIrInput in = tr_input(c, global_alpha);
    in.atlas = &c->atlas;
    tr_run_input(c, &in);
}

static int check_slot_translate_setup(TrCtx* c) {
    gpu_image_atlas_init(&c->atlas, c->out_arena);
    CHECK(gpu_image_atlas_place(&c->atlas, 3, 8, 8) != nullptr, "slot translate: atlas placement must succeed");

    gpu_image_slots_init(&c->slots, c->out_arena);
    gpu_image_slots_begin_frame(&c->slots);
    CHECK(gpu_image_slots_register_static(&c->slots, 7, 300, 40, 11) >= 0,
          "slot translate: large-image registration must succeed");
    return 0;
}

static int check_slot_mixed_kinds(TrCtx* c) {
    // One frame mixing all three image paths. Source sub-rects are in the texture's own texels.
    c->start = ir_begin(c->ir_arena);
    FlArena* a = c->ir_arena;
    emit_image_at(a, src_small, (FlRect) { 10, 10, 8, 8 }, FlImageFilter_Linear);
    emit_image_at(a, src_large, (FlRect) { 30, 10, 300, 40 }, FlImageFilter_Nearest);
    emit_image_at(a, src_dyn, (FlRect) { 30, 60, 4, 4 }, FlImageFilter_Linear);
    ir_image(a, src_large, FlImageMode_Color, (FlRect) { 30, 110, 50, 20 }, (FlRect) { 250, 20, 60, 30 }, white,
             FlImageFilter_Nearest, nullptr);
    tr_run_slots(c, 255);
    const GpuDrawList* list = &c->list;
    CHECK(list->count == 4, "slot translate: all three image paths translate");
    CHECK(list->cmds[0].kind == GpuDraw_Image, "slot translate: the atlas image keeps its kind");
    CHECK(list->cmds[1].kind == GpuDraw_SlotImage && list->cmds[1].aux == 0,
          "slot translate: the large static claims the first slot");
    return 0;
}

static int check_slot_translate_fields(TrCtx* c) {
    const GpuDrawList* list = &c->list;
    const GpuDrawCmd* large = &list->cmds[1];
    CHECK(large->rect[0] == 30.0f && large->rect[2] == 300.0f && large->rect[3] == 40.0f,
          "slot translate: the dst rect passes through");
    CHECK(radii_are(large, (FlRect) { 0.0f, 0.0f, 300.0f, 40.0f }),
          "slot translate: an empty src rect maps to the whole texture in its own texels");
    CHECK(list->cmds[2].kind == GpuDraw_SlotImage && list->cmds[2].aux == 1,
          "slot translate: the dynamic buffer claims the next slot");
    return 0;
}

static int check_slot_translate_reuse(TrCtx* c) {
    const GpuDrawList* list = &c->list;
    CHECK(list->cmds[3].aux == 0, "slot translate: a repeat reference reuses the image's slot");
    CHECK(radii_are(&list->cmds[3], (FlRect) { 250.0f, 20.0f, 300.0f, 40.0f }),
          "slot translate: an overhanging src sub-rect clamps to the texture extent");
    CHECK(list->aabbs[1].x0 == 30.0f && list->aabbs[1].x1 == 330.0f, "slot translate: the AABB is the dst rect");
    return 0;
}

static int check_slot_translate_table(TrCtx* c) {
    const GpuImageSlots* slots = &c->slots;
    CHECK(slots->slot_count == 2 && slots->slot_filter[0] == 1 && slots->slot_filter[1] == 0,
          "slot translate: the frame table holds both images with their first-reference filters");
    CHECK(slots->images[slots->slot_image[1]].dirty,
          "slot translate: the referenced dynamic buffer is marked for upload");
    CHECK(slots->overflow == 0, "slot translate: no overflow with free slots");
    return 0;
}

static int check_slot_tint_and_skips(TrCtx* c) {
    tr_begin(c);
    ir_image(c->ir_arena, src_large, FlImageMode_Color, (FlRect) { 0, 0, 10, 10 }, (FlRect) { 0 },
             (FlColor) { 200, 100, 50, 128 }, FlImageFilter_Linear, nullptr);
    tr_run_slots(c, 255);
    CHECK(c->list.cmds[0].color[0] == (100u | (50u << 8) | (25u << 16) | (128u << 24)),
          "slot translate: the tint packs premultiplied");

    tr_begin(c);
    emit_image_at(c->ir_arena, src_large, (FlRect) { 0, 0, 10, 10 }, FlImageFilter_Linear);
    emit_image_at(c->ir_arena, src_dyn, (FlRect) { 20, 0, 4, 4 }, FlImageFilter_Linear);
    tr_run_atlas_only(c, 255);
    CHECK(c->list.count == 0, "slot translate: without slot bookkeeping, slot-tier records skip");

    tr_begin(c);
    emit_image_at(c->ir_arena, (FlImageSource) { .kind = FlImageKind_Dynamic }, (FlRect) { 0, 0, 8, 8 },
                  FlImageFilter_Linear);
    emit_image_at(c->ir_arena,
                  (FlImageSource) { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_buf, .w = 0, .h = 4 } },
                  (FlRect) { 20, 0, 8, 8 }, FlImageFilter_Linear);
    tr_run_slots(c, 255);
    CHECK(c->list.count == 0 && c->slots.slot_count == 0, "slot translate: degenerate dynamic records skip");
    return 0;
}

static int check_slot_clipped_out(TrCtx* c) {
    static u8 dyn_clipped[4];
    u32 entries_before = c->slots.image_count;
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 8, 8 });
    emit_image_at(c->ir_arena, src_large, (FlRect) { 100, 100, 300, 40 }, FlImageFilter_Linear);
    emit_image_at(c->ir_arena,
                  (FlImageSource) { .kind = FlImageKind_Dynamic,
                                    .dynamic = { .pixels = dyn_clipped, .w = 1, .h = 1, .stride = 4 } },
                  (FlRect) { 200, 200, 4, 4 }, FlImageFilter_Linear);
    ir_clip_pop(c->ir_arena);
    tr_run_slots(c, 255);
    CHECK(c->list.count == 0 && c->slots.slot_count == 0 && c->slots.overflow == 0,
          "slot translate: fully clipped-out slot images claim nothing");
    CHECK(c->slots.image_count == entries_before,
          "slot translate: a clipped-out dynamic buffer never enters the table");
    return 0;
}

static int check_slot_translate_overflow(TrCtx* c) {
    // Slot overflow drops predictably: with GPU_IMAGE_SLOT_COUNT + 2 distinct dynamic buffers the trailing
    // two drop, and the two translation passes agree (the command list holds exactly the survivors, no
    // trailing garbage).
    static u8 bufs[GPU_IMAGE_SLOT_COUNT + 2][4];
    tr_begin(c);
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT + 2; i++) {
        emit_image_at(c->ir_arena,
                      (FlImageSource) { .kind = FlImageKind_Dynamic,
                                        .dynamic = { .pixels = bufs[i], .w = 1, .h = 1, .stride = 4 } },
                      (FlRect) { (f32)(i * 10), 0, 4, 4 }, FlImageFilter_Linear);
    }
    tr_run_slots(c, 255);
    CHECK(c->list.count == GPU_IMAGE_SLOT_COUNT, "slot overflow: exactly the slotted images translate");
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT; i++) {
        const GpuDrawCmd* cmd = &c->list.cmds[i];
        CHECK(cmd->kind == GpuDraw_SlotImage && cmd->aux == i && cmd->rect[0] == (f32)(i * 10),
              "slot overflow: survivors keep stream order with slots 0..N-1");
    }
    CHECK(c->slots.overflow == 2, "slot overflow: the counter counts both dropped commands");
    CHECK(c->slots.slot_count == GPU_IMAGE_SLOT_COUNT, "slot overflow: the frame table is exactly full");
    return 0;
}

int run_slot_translator_tests(void) {
    RUN_TR_SUITE("slot translator unit tests", check_slot_translate_setup, check_slot_mixed_kinds,
                 check_slot_translate_fields, check_slot_translate_reuse, check_slot_translate_table,
                 check_slot_tint_and_skips, check_slot_clipped_out, check_slot_translate_overflow);
}

// Background translator tests: the frame envelope's background state emits clear and fullscreen image commands
// at the front of the stream.

static const FlColor k_bg_clear = { 20, 30, 40, 255 };

static void bg_translate(TrCtx* c, const FlBackground* bg) {
    GpuIrInput in = { .bytes = 0, .global_alpha = 255, .target_width = TR_TARGET, .target_height = TR_TARGET };
    in.slots = &c->slots;
    in.background = bg;
    tr_run_input(c, &in);
}

static void bg_translate_with_ui(TrCtx* c, const FlBackground* bg, u8 global_alpha) {
    GpuIrInput in = tr_input(c, global_alpha);
    in.slots = &c->slots;
    in.background = bg;
    tr_run_input(c, &in);
}

static int check_background_setup(TrCtx* c) {
    gpu_image_slots_init(&c->slots, c->out_arena);
    gpu_image_slots_begin_frame(&c->slots);
    CHECK(gpu_image_slots_register_static(&c->slots, 7, 300, 40, 11) >= 0,
          "background translate: register a prev image");
    CHECK(gpu_image_slots_register_static(&c->slots, 8, 300, 40, 12) >= 0,
          "background translate: register a current image");
    return 0;
}

static int check_background_clear_only(TrCtx* c) {
    FlBackground bg_clear
        = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = k_bg_clear };
    bg_translate(c, &bg_clear);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1, "background: a clear-only envelope emits one command");
    CHECK(cmd->kind == GpuDraw_Rect, "background: the clear is a fill rect");
    CHECK(rect_words_are(cmd, (FlRect) { 0.0f, 0.0f, (f32)TR_TARGET, (f32)TR_TARGET }),
          "background: the clear covers the full target");
    return 0;
}

static int check_background_clear_color(TrCtx* c) {
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(cmd->color[0] == packed_rgba(k_bg_clear, 255) && cmd->clip == 0,
          "background: the clear carries the clear color at full opacity under the root clip");

    FlBackground bg_clearless
        = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = { 20, 30, 40, 0 } };
    bg_translate(c, &bg_clearless);
    CHECK(c->list.count == 0, "background: a zero-alpha clear emits no command");
    return 0;
}

static int check_background_single_image(TrCtx* c) {
    // A single image tints white at the background alpha, premultiplied: alpha 128 gives (128,128,128,128).
    FlBackground bg_one = { .current = 7, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 128, .clear = k_bg_clear };
    bg_translate(c, &bg_one);
    const GpuDrawList* list = &c->list;
    CHECK(list->count == 2, "background: clear + one image");
    CHECK(list->cmds[0].kind == GpuDraw_Rect, "background: the clear leads the image");
    CHECK(list->cmds[1].kind == GpuDraw_SlotImage && list->cmds[1].aux == 0,
          "background: the image rides the first sampler slot");
    return 0;
}

static int check_background_image_fields(TrCtx* c) {
    const GpuDrawCmd* image = &c->list.cmds[1];
    CHECK(image->rect[2] == (f32)TR_TARGET && image->rect[3] == (f32)TR_TARGET, "background: the image is fullscreen");
    CHECK(image->color[0] == (128u | (128u << 8) | (128u << 16) | (128u << 24)),
          "background: the single image tints white at the background alpha");

    FlBackground bg_one = { .current = 7, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 128, .clear = k_bg_clear };
    tr_begin(c);
    emit_solid_rect(c->ir_arena, (FlRect) { 10, 10, 50, 50 }, green);
    bg_translate_with_ui(c, &bg_one, 255);
    CHECK(c->list.count == 3, "background: clear + image + the UI rect");
    CHECK(c->list.cmds[2].kind == GpuDraw_Rect && c->list.cmds[2].rect[0] == 10.0f,
          "background: the UI command follows the background commands");
    return 0;
}

static int check_background_crossfade(TrCtx* c) {
    // A valid prev makes the background two images (prev opaque, current at crossfade_t), so over-compositing
    // yields lerp(prev, current, t). The background's own alpha is unused here.
    FlBackground bg_xf = { .current = 8, .prev = 7, .crossfade_t = 0.5f, .alpha = 200, .clear = k_bg_clear };
    bg_translate(c, &bg_xf);
    const GpuDrawList* list = &c->list;
    CHECK(list->count == 3, "crossfade: clear + prev + current");
    const GpuDrawCmd* prev = &list->cmds[1];
    CHECK(prev->kind == GpuDraw_SlotImage && prev->color[0] == 0xFFFFFFFFu,
          "crossfade: prev draws opaque (white tint)");
    const GpuDrawCmd* current = &list->cmds[2];
    CHECK(current->kind == GpuDraw_SlotImage && current->color[0] == (128u | (128u << 8) | (128u << 16) | (128u << 24)),
          "crossfade: current rides crossfade_t (0.5 -> 128) as its tint alpha, not the background alpha");
    CHECK(prev->aux != current->aux, "crossfade: prev and current occupy distinct slots");
    return 0;
}

static int check_background_global_alpha(TrCtx* c) {
    // The frame's global_alpha fades only the UI over the background, never the background itself.
    FlBackground bg_opaque = { .current = 7, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 255, .clear = k_bg_clear };
    tr_begin(c);
    emit_solid_rect(c->ir_arena, (FlRect) { 10, 10, 50, 50 }, green);
    bg_translate_with_ui(c, &bg_opaque, 128);
    CHECK(c->list.count == 3, "global_alpha: clear + image + UI");
    const u32 clear_alpha = c->list.cmds[0].color[0] >> 24;
    CHECK(clear_alpha == 255u, "global_alpha: the background clear stays opaque");
    const u32 image_alpha = c->list.cmds[1].color[0] >> 24;
    CHECK(image_alpha == 255u, "global_alpha: the background image tint ignores the frame global_alpha");
    const u32 ui_alpha = c->list.cmds[2].color[0] >> 24;
    CHECK(ui_alpha == 128u, "global_alpha: the UI over the background is faded");
    return 0;
}

int run_background_translator_tests(void) {
    RUN_TR_SUITE("background translator unit tests", check_background_setup, check_background_clear_only,
                 check_background_clear_color, check_background_single_image, check_background_image_fields,
                 check_background_crossfade, check_background_global_alpha);
}

// Text-run translator unit tests (no GL; the glyph-atlas bookkeeping is pure CPU): a run explodes to one
// GpuDraw_Glyph quad per non-empty prepared glyph, each positioned at the pen baseline offset by the cell's
// left/top metrics, sized to the padded cell, sampling the cell's texel rect and colorized by the run's
// premultiplied tint. The (font, size, glyph) triple is the atlas key.

enum { TEXT_FONT = 3, TEXT_SIZE = 24 };

static const FlColor k_run_color = { 220, 210, 40, 255 };
static const FlGlyph k_run[4] = {
    { .x = 100.0f, .y = 200.0f, .glyph_index = 'A' },
    { .x = 111.0f, .y = 200.0f, .glyph_index = ' ' }, // metrics-only: emits nothing
    { .x = 117.0f, .y = 200.0f, .glyph_index = 'b' },
    { .x = 125.0f, .y = 200.0f, .glyph_index = 'Z' }, // never prepared: skipped like an unresolved image
};

static const GpuGlyphEntry* text_entry(TrCtx* c, u32 glyph) {
    return gpu_glyph_atlas_find(&c->glyphs, TEXT_FONT, TEXT_SIZE, glyph);
}

static void tr_run_glyphs(TrCtx* c, u8 global_alpha) {
    GpuIrInput in = tr_input(c, global_alpha);
    in.glyphs = &c->glyphs;
    tr_run_input(c, &in);
}

static void emit_run(TrCtx* c, u16 size, FlColor color) {
    ir_text_run(c->ir_arena, TEXT_FONT, size, 1.0f, color, (FlGlyph*)k_run, 4);
}

// Premultiplied tint pack for a color at a global alpha, mirroring the translator's rounding.
static u32 premultiplied_pack(FlColor color, u32 alpha_scale) {
    u32 a = ((u32)color.a * alpha_scale + 127u) / 255u;
    u32 r = ((u32)color.r * a + 127u) / 255u;
    u32 g = ((u32)color.g * a + 127u) / 255u;
    u32 b = ((u32)color.b * a + 127u) / 255u;
    return r | (g << 8) | (b << 16) | (a << 24);
}

static bool aabb_bounds_quad(const GpuAabb* box, const GpuDrawCmd* cmd) {
    return box->x0 == cmd->rect[0] && box->y0 == cmd->rect[1] && box->x1 == cmd->rect[0] + cmd->rect[2]
           && box->y1 == cmd->rect[1] + cmd->rect[3];
}

static bool source_rect_is_cell(const GpuDrawCmd* cmd, const GpuGlyphEntry* g) {
    return radii_are(
        cmd, (FlRect) { (f32)g->cell.x, (f32)g->cell.y, (f32)(g->cell.x + g->cell.w), (f32)(g->cell.y + g->cell.h) });
}

static int check_text_setup(TrCtx* c) {
    gpu_glyph_atlas_init(&c->glyphs, c->out_arena);
    const GpuGlyphEntry* g_a = gpu_glyph_atlas_place(&c->glyphs, (GpuGlyphKey) { TEXT_FONT, TEXT_SIZE, 'A' },
                                                     (GpuGlyphMetrics) { 10, 14, -1.0f, 12.0f, 11.0f });
    const GpuGlyphEntry* g_b = gpu_glyph_atlas_place(&c->glyphs, (GpuGlyphKey) { TEXT_FONT, TEXT_SIZE, 'b' },
                                                     (GpuGlyphMetrics) { 8, 16, 0.0f, 14.0f, 9.0f });
    gpu_glyph_atlas_place(&c->glyphs, (GpuGlyphKey) { TEXT_FONT, TEXT_SIZE, ' ' },
                          (GpuGlyphMetrics) { 0, 0, 0.0f, 0.0f, 6.0f }); // empty (metrics-only) glyph
    CHECK(g_a != nullptr && g_b != nullptr, "text translate: the setup glyphs place");
    return 0;
}

static int check_text_run_quads(TrCtx* c) {
    c->start = ir_begin(c->ir_arena);
    emit_run(c, TEXT_SIZE, k_run_color);
    tr_run_glyphs(c, 255);
    CHECK(c->list.count == 2, "text translate: a run explodes to one quad per non-empty prepared glyph");

    // At full alpha the premultiplied run tint is identical to the straight pack.
    const GpuGlyphEntry* g_a = text_entry(c, 'A');
    const GpuDrawCmd* a = &c->list.cmds[0];
    CHECK(a->kind == GpuDraw_Glyph, "text translate: a glyph emits GpuDraw_Glyph");
    CHECK(a->rect[0] == 100.0f + g_a->left && a->rect[1] == 200.0f - g_a->top,
          "text translate: the quad sits at the pen baseline offset by left/top");
    CHECK(a->rect[2] == (f32)g_a->cell.w && a->rect[3] == (f32)g_a->cell.h,
          "text translate: the quad is the padded cell size");
    return 0;
}

static int check_text_second_glyph(TrCtx* c) {
    const GpuDrawCmd* a = &c->list.cmds[0];
    CHECK(aabb_bounds_quad(&c->list.aabbs[0], a), "text translate: each glyph's AABB bounds its own quad");

    const GpuGlyphEntry* g_b = text_entry(c, 'b');
    const GpuDrawCmd* b = &c->list.cmds[1];
    CHECK(b->kind == GpuDraw_Glyph && b->rect[0] == 117.0f + g_b->left && b->rect[1] == 200.0f - g_b->top,
          "text translate: the empty and unprepared glyphs are skipped, 'b' follows 'A'");
    CHECK(aabb_bounds_quad(&c->list.aabbs[1], b), "text translate: 'b''s AABB bounds its own quad, not the run");
    return 0;
}

static int check_text_quad_fields(TrCtx* c) {
    const GpuGlyphEntry* g_a = text_entry(c, 'A');
    const GpuDrawCmd* a = &c->list.cmds[0];
    CHECK(source_rect_is_cell(a, g_a), "text translate: radius carries the cell's source texel rect");
    CHECK(a->color[0] == packed_rgba(k_run_color, 255), "text translate: an opaque run colorizes with its tint");
    CHECK(a->clip == 0, "text translate: glyphs stamp the root clip");
    return check_text_second_glyph(c);
}

static int check_text_tint_and_missing(TrCtx* c) {
    const FlColor half = { 200, 100, 50, 128 };
    tr_begin(c);
    FlGlyph one = { .x = 0.0f, .y = 50.0f, .glyph_index = 'A' };
    ir_text_run(c->ir_arena, TEXT_FONT, TEXT_SIZE, 1.0f, half, &one, 1);
    tr_run_glyphs(c, 255);
    CHECK(c->list.count == 1 && c->list.cmds[0].color[0] == premultiplied_pack(half, 255),
          "text translate: the run tint packs premultiplied (alpha-coverage colorization)");

    tr_begin(c);
    emit_run(c, (u16)(TEXT_SIZE + 2), k_run_color);
    tr_run_glyphs(c, 255);
    CHECK(c->list.count == 0, "text translate: a run at an unprepared size resolves no glyphs");

    tr_begin(c);
    emit_run(c, TEXT_SIZE, k_run_color);
    GpuIrInput no_atlas = tr_input(c, 255);
    tr_run_input(c, &no_atlas);
    CHECK(c->list.count == 0, "text translate: a nullptr glyph atlas emits no text commands");
    return 0;
}

static int check_text_clips(TrCtx* c) {
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0.0f, 0.0f, 4.0f, 4.0f }); // a tiny box far from the glyphs at ~(100, 200)
    emit_run(c, TEXT_SIZE, k_run_color);
    ir_clip_pop(c->ir_arena);
    tr_run_glyphs(c, 255);
    CHECK(c->list.count == 0, "text translate: glyphs fully outside the clip drop");

    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 90.0f, 180.0f, 80.0f, 40.0f }); // covers 'A' (~100,200) and 'b' (~117,200)
    emit_run(c, TEXT_SIZE, k_run_color);
    ir_clip_pop(c->ir_arena);
    tr_run_glyphs(c, 255);
    CHECK(c->list.count == 2 && c->list.cmds[0].clip != 0 && c->list.cmds[0].clip == c->list.cmds[1].clip,
          "text translate: glyphs inside a clip survive and inherit its (non-root) clip index");

    tr_begin(c);
    FlGlyph ga = { .x = 0.0f, .y = 50.0f, .glyph_index = 'A' };
    ir_text_run(c->ir_arena, TEXT_FONT, TEXT_SIZE, 1.0f, k_run_color, &ga, 1);
    tr_run_glyphs(c, 128);
    CHECK(c->list.count == 1 && c->list.cmds[0].color[0] == premultiplied_pack(k_run_color, 128),
          "text translate: the frame global_alpha folds into the run tint (premultiplied)");
    return 0;
}

static int check_text_scale_source(TrCtx* c, const GpuGlyphEntry* g_a) {
    const GpuDrawCmd* sc = &c->list.cmds[0];
    CHECK(source_rect_is_cell(sc, g_a),
          "text translate: the source texel rect is unscaled — the quad magnifies the same raster cell");
    CHECK(c->list.aabbs[0].x1 == sc->rect[0] + sc->rect[2] && c->list.aabbs[0].y1 == sc->rect[1] + sc->rect[3],
          "text translate: the AABB bounds the scaled quad");
    return 0;
}

static int check_text_scale(TrCtx* c) {
    // Display scale: run->scale multiplies each glyph quad's bearing offset and extent around the pen, while
    // the atlas source rect (radius[]) stays in raster texels so the shader's linear sample magnifies the
    // cell's coverage. A scale != 1 leaves the run rasterized at size but displays it at size * scale.
    const f32 s = 1.5f;
    tr_begin(c);
    FlGlyph scaled = { .x = 100.0f, .y = 200.0f, .glyph_index = 'A' };
    ir_text_run(c->ir_arena, TEXT_FONT, TEXT_SIZE, s, k_run_color, &scaled, 1);
    tr_run_glyphs(c, 255);
    CHECK(c->list.count == 1 && c->list.cmds[0].kind == GpuDraw_Glyph,
          "text translate: a scaled run still emits a glyph quad");
    const GpuGlyphEntry* g_a = text_entry(c, 'A');
    const GpuDrawCmd* sc = &c->list.cmds[0];
    CHECK(sc->rect[0] == 100.0f + g_a->left * s && sc->rect[1] == 200.0f - g_a->top * s,
          "text translate: scale multiplies the bearing offset around the pen");
    CHECK(sc->rect[2] == (f32)g_a->cell.w * s && sc->rect[3] == (f32)g_a->cell.h * s,
          "text translate: scale multiplies the quad extent (display size = raster size * scale)");
    return check_text_scale_source(c, g_a);
}

static int check_text_effect(TrCtx* c) {
    // A text run's effect sits past the inline FlGlyph array, not right after the payload. Reading it at the
    // wrong offset picks up glyph bytes. ir_text_run takes no effect, so assemble the record directly.
    const FlColor tfx_color = { 9, 87, 65, 255 };
    FlEffect text_fx
        = { .kind = FlEffectKind_DropShadow, .color = tfx_color, .off_x = 2.0f, .off_y = 2.0f, .blur = 1.0f };
    tr_begin(c);
    FlCmdHeader* theader
        = ir_header(c->ir_arena, FlCmd_TextRun,
                    (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdTextRun) + sizeof(FlGlyph)) + ir_effect_bytes(&text_fx));
    FlCmdTextRun* trun = arena_alloc_zero(c->ir_arena, FlCmdTextRun);
    trun->font = TEXT_FONT;
    trun->size = TEXT_SIZE;
    trun->color = k_run_color;
    trun->glyph_count = 1;
    trun->scale = 1.0f;
    FlGlyph* trun_glyphs = arena_alloc_array(c->ir_arena, FlGlyph, 1);
    trun_glyphs[0] = (FlGlyph) { .x = 100.0f, .y = 200.0f, .glyph_index = 'A' };
    ir_append_effect(c->ir_arena, theader, &text_fx);
    tr_run_glyphs(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1 && cmd->kind == GpuDraw_Glyph, "text effect: the annotated run translates");
    CHECK(effect_kind_of(cmd) == 1 && cmd->effect[3] == packed_rgba(tfx_color, 255),
          "text effect: the effect is located past the glyph array and stamped onto the quad");
    CHECK(bits_f32(cmd->effect[0]) == 2.0f && bits_f32(cmd->effect[1]) == 2.0f,
          "text effect: the offset words round-trip, proving the read landed on the FlEffect");
    return 0;
}

int run_text_translator_tests(void) {
    RUN_TR_SUITE("text translator unit tests", check_text_setup, check_text_run_quads, check_text_quad_fields,
                 check_text_tint_and_missing, check_text_clips, check_text_scale, check_text_effect);
}

// SVG translator (pure CPU, no GL): a static handle that resolves to the R8 SVG atlas (not the image atlas,
// not a slot) emits a GpuDraw_Svg quad carrying the cell's padding-inset source sub-rect and the colorized
// tint. Opaque white keeps the icon's authored fill; any other tint recolors.

enum { SVG_HANDLE = 77, SVG_COLOR_HANDLE = 91 };

static const FlColor k_svg_authored = { 50, 150, 200, 255 };
static const FlRect k_svg_dst = { 100.0f, 120.0f, 32.0f, 24.0f };

static void emit_svg_draw(TrCtx* c, FlImageHandle handle, FlColor tint) {
    tr_begin(c);
    ir_image(c->ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = handle }, FlImageMode_Color,
             k_svg_dst, (FlRect) { 0 }, tint, FlImageFilter_Linear, nullptr);
}

static void tr_run_svg(TrCtx* c, u8 global_alpha) {
    GpuIrInput in = tr_input(c, global_alpha);
    in.svg = &c->svg;
    tr_run_input(c, &in);
}

static void tr_run_svg_color(TrCtx* c, u8 global_alpha) {
    GpuIrInput in = tr_input(c, global_alpha);
    in.svg_color = &c->svg_color;
    tr_run_input(c, &in);
}

static bool radii_are_padded_content(const GpuDrawCmd* cmd, const GpuSvgEntry* cell) {
    return radii_are(cmd, (FlRect) { (f32)(cell->cell.x + GPU_SVG_ATLAS_PAD), (f32)(cell->cell.y + GPU_SVG_ATLAS_PAD),
                                     (f32)(cell->cell.x + cell->cell.w - GPU_SVG_ATLAS_PAD),
                                     (f32)(cell->cell.y + cell->cell.h - GPU_SVG_ATLAS_PAD) });
}

static int check_svg_setup(TrCtx* c) {
    gpu_svg_atlas_init(&c->svg, c->out_arena, GpuSvgTexel_R8);
    // A 34 x 26 padded cell: a 32 x 24 content block plus the 1-texel ring (the aspect-fit of a 40x30 icon at 32).
    c->svg_cell = gpu_svg_atlas_place(
        &c->svg, &(GpuSvgPlaceRequest) { SVG_HANDLE, 32, 34, 26, FlVectorClass_Monochrome, k_svg_authored, nullptr });
    CHECK(c->svg_cell != nullptr, "svg translate: the setup icon places into the atlas");
    return 0;
}

static int check_svg_quad(TrCtx* c) {
    // The authored fill is opaque, so the premultiply leaves rgb intact and equals the straight pack.
    c->start = ir_begin(c->ir_arena);
    ir_image(c->ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = SVG_HANDLE }, FlImageMode_Color,
             k_svg_dst, (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    tr_run_svg(c, 255);
    CHECK(c->list.count == 1, "svg translate: a vector-mono handle emits one command");
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(cmd->kind == GpuDraw_Svg, "svg translate: a vector-mono handle emits GpuDraw_Svg");
    CHECK(rect_words_are(cmd, k_svg_dst), "svg translate: the quad is the dst rect");
    return 0;
}

static int check_svg_quad_fields(TrCtx* c) {
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(radii_are_padded_content(cmd, c->svg_cell),
          "svg translate: radius carries the cell's padding-inset content sub-rect");
    CHECK(cmd->color[0] == packed_rgba(k_svg_authored, 255),
          "svg translate: an opaque-white tint colorizes with the icon's authored fill");
    CHECK(cmd->clip == 0, "svg translate: the icon stamps the root clip");
    return 0;
}

static int check_svg_tints(TrCtx* c) {
    const FlColor override = { 230, 80, 200, 255 };
    emit_svg_draw(c, SVG_HANDLE, override);
    tr_run_svg(c, 255);
    CHECK(c->list.count == 1 && c->list.cmds[0].kind == GpuDraw_Svg
              && c->list.cmds[0].color[0] == packed_rgba(override, 255),
          "svg translate: a non-identity tint recolors the icon");

    emit_svg_draw(c, SVG_HANDLE, white);
    tr_run_svg(c, 128);
    CHECK(c->list.count == 1 && c->list.cmds[0].color[0] == premultiplied_pack(k_svg_authored, 128),
          "svg translate: the frame global_alpha folds into the tint (premultiplied)");

    // A handle that lives only in the SVG tier drops rather than falling through to a slot or misrouting.
    emit_svg_draw(c, SVG_HANDLE, white);
    GpuIrInput no_svg = tr_input(c, 255);
    tr_run_input(c, &no_svg);
    CHECK(c->list.count == 0, "svg translate: a nullptr SVG atlas emits no command for a vector-only handle");
    return 0;
}

static int check_svg_color_quad(TrCtx* c) {
    // Multi-color path: a handle living in the RGBA8 color atlas emits GpuDraw_SvgColor sampling the baked
    // premultiplied cell, modulated by a white tint. The colors are baked, so a record tint is ignored.
    gpu_svg_atlas_init(&c->svg_color, c->out_arena, GpuSvgTexel_Rgba8);
    c->svg_color_cell = gpu_svg_atlas_place(
        &c->svg_color,
        &(GpuSvgPlaceRequest) { SVG_COLOR_HANDLE, 32, 34, 26, FlVectorClass_MultiColor, (FlColor) { 0 }, nullptr });
    CHECK(c->svg_color_cell != nullptr, "svg color translate: the icon places into the RGBA8 atlas");

    emit_svg_draw(c, SVG_COLOR_HANDLE, (FlColor) { 230, 80, 200, 255 });
    tr_run_svg_color(c, 255);
    CHECK(c->list.count == 1, "svg color translate: a multi-color handle emits one command");
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(cmd->kind == GpuDraw_SvgColor, "svg color translate: a multi-color handle emits GpuDraw_SvgColor");
    CHECK(rect_words_are(cmd, k_svg_dst), "svg color translate: the quad is the dst rect");
    return 0;
}

static int check_svg_color_fields(TrCtx* c) {
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(radii_are_padded_content(cmd, c->svg_color_cell),
          "svg color translate: radius carries the cell's padding-inset content sub-rect");
    CHECK(cmd->color[0] == 0xFFFFFFFFu,
          "svg color translate: the baked cell samples under opaque white (a record tint is ignored)");

    // The frame global_alpha is applied per frame to the persistent premultiplied cell, never baked into it.
    emit_svg_draw(c, SVG_COLOR_HANDLE, white);
    tr_run_svg_color(c, 128);
    CHECK(c->list.count == 1 && c->list.cmds[0].color[0] == (128u | (128u << 8) | (128u << 16) | (128u << 24)),
          "svg color translate: the frame global_alpha rides the white tint premultiplied");
    return 0;
}

int run_svg_translator_tests(void) {
    RUN_TR_SUITE("svg translator unit tests", check_svg_setup, check_svg_quad, check_svg_quad_fields, check_svg_tints,
                 check_svg_color_quad, check_svg_color_fields);
}

#endif // !defined(__APPLE__)
