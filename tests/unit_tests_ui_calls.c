// Public UI call, frame envelope, widget id and hit clip tests

#include "widgets/common.h"
#include <math.h>

UTEST(direct_ui_calls, calls_reach_real_bodies_in_a_live_frame) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_painter_rect(0.0f, 0.0f, 16.0f, 16.0f, ui_color(200, 30, 40, 255));
        FlSignal sig = fl_button_impl(FL_S("ok"), (FlWidgetConfig) { 0 });
        ASSERT_TRUE_MSG(!sig.clicked, "no input was injected, so the button must not report a click");
        bool checked = false;
        fl_checkbox_impl(FL_S("on"), &checked, (FlCheckboxConfig) { 0 });
        bool toggled = false;
        fl_switch_impl(FL_S("en"), &toggled, (FlSwitchConfig) { 0 });
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The public measurement calls: font_id 0 means the active font.
UTEST(direct_ui_calls, text_measure_resolves_the_active_font) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);
    FlFontId font = font_cache_load_default_font(ctx->shared->font_cache, 2);
    ui_font_set(font);

    ui_update_begin(ctx);
    FlTextMetrics by_active = fl_text_measure(FL_S("AA"), (FlTextStyle) { 0 });
    FlTextMetrics by_id = fl_text_measure(FL_S("AA"), (FlTextStyle) { .font_id = font });
    ASSERT_NEAR_MSG(by_active.width, 32.0f, 0.0001f, "two 8x8 cells at scale 2 through the public entry point");
    ASSERT_NEAR_MSG(by_id.width, by_active.width, 0.0001f, "font_id 0 resolves to the active font");
    FlFontMetrics fm = fl_font_metrics(0, 0.0f);
    ASSERT_NEAR_MSG(fm.line_height, 16.0f, 0.0001f, "font metrics resolve font id 0 to the active font");
    ui_update_end(ctx, 1.0f / 60.0f);
}

// A real frame passes the context's text_scale to the painter, and the public measure API does not apply it. An
// app authors in logical pixels and feeds the measured results back into layout, which applies the scale, so
// scaling in the measure API would apply it twice.
UTEST(direct_ui_calls, a_frame_scales_painter_text_but_not_the_measure_api) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    // A FreeType face is needed here. The built-in default font reports the same metrics at every size, so it
    // cannot tell a scaled query from an unscaled one.
    const u16 authored = 16;
    FlFontId font
        = font_cache_load_font_memory(ctx->shared->font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, authored);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");
    ui_font_set(font);

    ui_update_begin(ctx);
    FlTextMetrics measured_at_1 = fl_text_measure(FL_S("AA"), (FlTextStyle) { .font_id = font, .font_size = 12.0f });
    FlFontMetrics vmetrics_at_1 = fl_font_metrics(font, 12.0f);
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_TRUE_MSG(measured_at_1.width > 0.0f && vmetrics_at_1.line_height > 0.0f,
                    "both measurements are non-zero, so comparing them means something");

    ui_set_scale(2.0f, 2.0f);

    const FlPaintSpan* drawn = nullptr;
    ui_update_begin(ctx);
    FlTextMetrics measured_at_2 = fl_text_measure(FL_S("AA"), (FlTextStyle) { .font_id = font, .font_size = 12.0f });
    FlFontMetrics vmetrics_at_2 = fl_font_metrics(font, 12.0f);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = g_ui_ctx->painter;
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot = nullptr;
        painter_scope_push(p, &slot);
        // font_size 0 takes the authored load size, the path that depends most on the scale reaching the
        // painter.
        fl_painter_text_styled(4.0f, 4.0f, FL_S("AA"), (FlTextStyle) { .font_id = font }, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        drawn = (const FlPaintSpan*)slot;
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(drawn != nullptr && drawn->first != nullptr, "the styled draw attached a paint span");
    const FlCmdTextRun* run = (const FlCmdTextRun*)((const FlCmdHeader*)drawn->first->ir + 1);
    ASSERT_EQ_MSG((int)run->size, (int)font_cache_quantize_size_q4((float)authored * 2.0f),
                  "ui_update_begin hands the context's text_scale to the painter, so the run rasterizes at 2x");

    ASSERT_NEAR_MSG(measured_at_2.width, measured_at_1.width, 0.0001f,
                    "fl_text_measure stays in logical space; layout applies the scale, and doing it here too "
                    "would apply it twice");
    ASSERT_NEAR_MSG(vmetrics_at_2.line_height, vmetrics_at_1.line_height, 0.0001f,
                    "fl_font_metrics stays logical for the same reason");
}

// The painter's styled draw call treats font_id 0 as the active font, the same rule fl_text_measure applies, so
// draw and measure agree.
UTEST(direct_ui_calls, painter_text_styled_resolves_the_active_font) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);
    FlFontId font = font_cache_load_default_font(ctx->shared->font_cache, 2);
    ui_font_set(font);

    ui_update_begin(ctx);
    const FlPaintSpan* by_active = nullptr;
    const FlPaintSpan* by_id = nullptr;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = g_ui_ctx->painter;
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot_active = nullptr;
        painter_scope_push(p, &slot_active);
        fl_painter_text_styled(4.0f, 4.0f, FL_S("AA"), (FlTextStyle) { 0 }, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        void* slot_id = nullptr;
        painter_scope_push(p, &slot_id);
        fl_painter_text_styled(4.0f, 4.0f, FL_S("AA"), (FlTextStyle) { .font_id = font }, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        // The plain painter text call follows the same rule: font 0 draws with the active font.
        void* slot_plain = nullptr;
        painter_scope_push(p, &slot_plain);
        fl_painter_text(4.0f, 4.0f, FL_S("AA"), 0, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        const FlPaintSpan* by_plain = (const FlPaintSpan*)slot_plain;
        ASSERT_TRUE_MSG(by_plain != nullptr && by_plain->first != nullptr, "plain text with font 0 draws");
        const FlCmdTextRun* run_plain = (const FlCmdTextRun*)((const FlCmdHeader*)by_plain->first->ir + 1);
        ASSERT_EQ_MSG((int)run_plain->font, (int)font, "fl_painter_text resolves font 0 to the active font");
        by_active = (const FlPaintSpan*)slot_active;
        by_id = (const FlPaintSpan*)slot_id;
    }

    ASSERT_TRUE_MSG(by_active != nullptr && by_active->first != nullptr, "font_id 0 draws through the active font");
    ASSERT_TRUE_MSG(by_id != nullptr && by_id->first != nullptr, "an explicit font id draws");
    const FlCmdTextRun* run_active = (const FlCmdTextRun*)((const FlCmdHeader*)by_active->first->ir + 1);
    const FlCmdTextRun* run_id = (const FlCmdTextRun*)((const FlCmdHeader*)by_id->first->ir + 1);
    ASSERT_EQ_MSG((int)run_active->font, (int)font, "font_id 0 resolves to the active font");
    ASSERT_EQ_MSG((int)run_id->font, (int)run_active->font, "both spellings resolve to the same font");
    const FlGlyph* g_active = (const FlGlyph*)(run_active + 1);
    const FlGlyph* g_id = (const FlGlyph*)(run_id + 1);
    ASSERT_TRUE_MSG(g_active[0].x == g_id[0].x && g_active[0].y == g_id[0].y && g_active[1].x == g_id[1].x,
                    "both spellings lay the run out identically");
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The public effect call reaches the painter inside a live frame, when no scope is open.
UTEST(direct_ui_calls, painter_set_effect_reaches_the_body) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    ui_update_begin(ctx);
    const FlPaintSpan* painted = nullptr;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = g_ui_ctx->painter;
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot = nullptr;
        painter_scope_push(p, &slot);
        fl_painter_set_effect(
            (FlEffect) { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_y = 24.0f, .blur = 70.0f });
        fl_painter_rect(0.0f, 0.0f, 8.0f, 8.0f, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        painted = (const FlPaintSpan*)slot;
    }

    ASSERT_TRUE_MSG(painted != nullptr && painted->first != nullptr, "the public painter calls drew");
    const FlCmdHeader* h = (const FlCmdHeader*)painted->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_HAS_EFFECT) != 0,
                    "fl_painter_set_effect reaches the body: the later rect carries the effect");
    const FlEffect* fx = (const FlEffect*)((const FlCmdFillRect*)(h + 1) + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_DropShadow && fx->off_y == 24.0f && fx->blur == 70.0f,
                    "the effect crosses the public seam intact");
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The public bar-row call reaches the painter inside a live frame, where the whole row (pointer and count)
// becomes one batched record.
UTEST(direct_ui_calls, painter_bars_reaches_the_body) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    ui_update_begin(ctx);
    const FlPaintSpan* painted = nullptr;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = g_ui_ctx->painter;
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot = nullptr;
        painter_scope_push(p, &slot);
        FlBar bars[3] = { { 0.0f, 2.0f, 10.0f }, { 3.0f, 2.0f, -4.0f }, { 6.0f, 2.0f, 7.0f } };
        fl_painter_bars(100.0f, bars, 3, ui_color(200, 100, 50, 255));
        painter_scope_pop(p);
        painted = (const FlPaintSpan*)slot;
    }

    ASSERT_TRUE_MSG(painted != nullptr && painted->first != nullptr, "the public bar row drew");
    const FlCmdHeader* h = (const FlCmdHeader*)painted->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_Bar, "one batched bar-set record carries the whole row");
    const FlCmdBarSet* bar_set = (const FlCmdBarSet*)(h + 1);
    ASSERT_TRUE_MSG(bar_set->baseline_y == 100.0f, "the baseline crosses the public seam");
    ASSERT_EQ_MSG(bar_set->bar_count, 3u, "the slice length becomes the record's bar count");
    const FlBar* rbars = (const FlBar*)(bar_set + 1);
    ASSERT_TRUE_MSG(rbars[2].x == 6.0f && rbars[2].w == 2.0f && rbars[2].h == 7.0f,
                    "the last bar survives the pointer+count seam, so the whole row does");
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(frame_api, setter_updates_persistent_frame_envelope_alpha) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_EQ_MSG(ctx->global_alpha, 255, "frame opacity defaults to fully opaque");

    fl_frame_set_alpha(173);
    ASSERT_EQ_MSG(ctx->global_alpha, 173, "direct frame setter updates the envelope source");

    fl_frame_set_alpha(91);
    ASSERT_EQ_MSG(ctx->global_alpha, 91, "frame setter reaches the body");

    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(s_render.submitted_global_alpha, 91, "submitted frame envelope captures the requested opacity");
    ASSERT_EQ_MSG(ctx->global_alpha, 91, "frame opacity persists across submission until changed");
    ASSERT_EQ_MSG(s_render.submitted_background.alpha, 255,
                  "frame opacity does not modify background composition opacity");

    fl_frame_set_alpha(255);
}

// ui_item_id_seed gives each item of a multi-item widget (radio group, tab strip) its id seed. It spreads the
// instance base so the item ranges of two nearby instance keys do not overlap, and for a fixed key it is a
// bijection in the index, so items within one widget never share an id.

static void assert_all_distinct(int* utest_result, const u32* values, int count, const char* message) {
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            ASSERT_NE_MSG(values[i], values[j], message);
        }
    }
}

UTEST(widget_id, seed_is_bijection_within_one_instance) {
    // For a fixed instance key, distinct item indices map to distinct seeds. Otherwise two items in the same
    // widget would share per-item state and trip the duplicate-id assert.
    const u32 key = 0x00400000u;
    enum { N = 512 };
    static u32 seeds[N];
    for (u32 i = 0; i < N; i++) {
        seeds[i] = ui_item_id_seed(key, i);
    }
    assert_all_distinct(utest_result, seeds, N, "per-item seeds within one instance must be unique");
}

UTEST(widget_id, adjacent_instance_keys_do_not_collide) {
    // Covers the small gaps between adjacent 4- and 8-byte fields, with item counts well past any real radio
    // group or tab bar.
    const u32 base = 0x7ffe0100u;
    const u32 gaps[] = { 1, 2, 4, 8, 12, 16 };
    enum { ITEMS = 64 };
    for (size_t g = 0; g < sizeof_array(gaps); g++) {
        const u32 key_a = base;
        const u32 key_b = base + gaps[g];
        static u32 both[2 * ITEMS];
        int n = 0;
        for (u32 i = 0; i < ITEMS; i++) {
            both[n++] = ui_item_id_seed(key_a, i);
        }
        for (u32 i = 0; i < ITEMS; i++) {
            both[n++] = ui_item_id_seed(key_b, i);
        }
        assert_all_distinct(utest_result, both, n, "per-item seeds must not collide across adjacent instance keys");
        RETURN_IF_TEST_FAILED();
    }
}

// Hit testing is clipped. Clay culls against the screen and not against the clip rect, so an element that
// overflows its clip container reaches the hit test with its full bounding box.

// 60x120 placed at y=140 inside a 200x200 clip box, so 60px of it are inside the clip and 60px are cut away.
static void hit_clip_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hc_clip"), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                          .color = ui_color(50, 50, 60, 255),
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
            ui_layout_area_id(ID("hc_item"), .sizing = { sizing_fixed(60), sizing_fixed(120) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 20.0f, 140.0f } }) {
                ui_item_behavior(ID("hc_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
            // Entirely cut away by the clip box, while its own rect is well inside the 400x400 window.
            ui_layout_area_id(ID("hc_hidden"), .sizing = { sizing_fixed(60), sizing_fixed(60) },
                              .color = ui_color(200, 60, 40, 255),
                              .placed = { .enabled = true, .offset = { 20.0f, 260.0f } }) {
                ui_item_behavior(ID("hc_hidden"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(hit_clip, an_overflowing_element_takes_input_only_where_it_paints) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitclip"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f; // sizing_fixed() multiplies by ui_scale, so 1.0 keeps the rects exact
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_clip_emit(ctx);

    const u32 item_id = ui_layout_id_to_clay_id(ID("hc_item"));
    Clay_ElementData box = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("hc_clip")) });
    Clay_ElementData item = Clay_GetElementData((Clay_ElementId) { .id = item_id });
    ASSERT_TRUE_MSG(box.found && item.found, "the clip container and its child must lay out");

    const f32 clip_bottom = box.boundingBox.y + box.boundingBox.height;
    const f32 item_bottom = item.boundingBox.y + item.boundingBox.height;
    ASSERT_GT_MSG(item_bottom, clip_bottom, "the child must overflow its clip container for this to test anything");
    ASSERT_LT_MSG(item.boundingBox.y, clip_bottom, "and part of it must survive the clip");

    const f32 x = item.boundingBox.x + item.boundingBox.width * 0.5f;

    ctx->input_state.mouse.x = x;
    ctx->input_state.mouse.y = (item.boundingBox.y + clip_bottom) * 0.5f;
    hit_clip_emit(ctx);
    ASSERT_EQ_MSG(item_id, ctx->hot_id, "over the part that survives the clip, the element is hot");

    ctx->input_state.mouse.y = (clip_bottom + item_bottom) * 0.5f;
    hit_clip_emit(ctx);
    ASSERT_NE_MSG(item_id, ctx->hot_id, "past the clip it paints nothing, so it must take no input either");

    // FlElementState.visible uses the same clip chain: it means inside the clip region, not only on screen.
    Clay_ElementData hidden_box
        = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("hc_hidden")) });
    ASSERT_TRUE_MSG(hidden_box.found, "the fully clipped element must still lay out");
    ASSERT_GT_MSG(hidden_box.boundingBox.y, clip_bottom, "it must sit entirely past the clip");
    ASSERT_LT_MSG(hidden_box.boundingBox.y + hidden_box.boundingBox.height, ctx->layout_height,
                  "while its own rect stays on screen, where the screen test alone would call it visible");

    FlElementState hidden = ui_get_element_state(ID("hc_hidden"));
    FlElementState shown = ui_get_element_state(ID("hc_item"));
    ASSERT_TRUE_MSG(hidden.found && shown.found, "both elements must report state");
    ASSERT_FALSE_MSG(hidden.visible, "an element the clip chain cuts away entirely is not visible");
    ASSERT_TRUE_MSG(shown.visible, "an element that survives the clip in part stays visible");

    fl_app_destroy(app);
}

// An element with both a background and a clip gets two render commands under one id. Its hit-test state still
// advances once per frame, so the focus-gained edge is reported.

static void hit_once_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(ID("ho_item"), .sizing = { sizing_fixed(100), sizing_fixed(100) },
                          .color = ui_color(50, 50, 60, 255),
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
            ui_item_behavior(ID("ho_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(hit_clip, a_background_and_clip_update_the_item_once_per_frame) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitonce"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_once_emit(ctx);
    const u32 item_id = ui_layout_id_to_clay_id(ID("ho_item"));
    ItemState* item = hashmap_get(&ctx->shared->item_state, item_id);
    ASSERT_TRUE_MSG(item != nullptr, "the behavior call must create the item");
    ASSERT_FALSE(item->state.focused);

    const u64 pass_before = ctx->shared->hit_test_pass;
    ctx->focused_id = item_id;
    hit_once_emit(ctx);
    ASSERT_EQ_MSG(ctx->shared->hit_test_pass, pass_before + 1, "one hit-test pass per frame");
    ASSERT_EQ_MSG(item->hit_pass, ctx->shared->hit_test_pass, "the item was updated in that pass");
    ASSERT_TRUE_MSG(item->state.focused, "focus is picked up in the frame it is set");
    ASSERT_FALSE_MSG(item->was_focused, "the focus-gained edge must survive the element's second command");

    hit_once_emit(ctx);
    ASSERT_TRUE_MSG(item->was_focused, "the edge is consumed by the next frame's update");

    ctx->focused_id = 0;
    fl_app_destroy(app);
}

// Clay emits an overlay-color marker under the element's id, with a zero box, ahead of the element's own
// commands. The marker has no geometry, so it does not count as the element's hit-test update.

static void hit_overlay_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_layout_start(ID("hov_item"));
        Clay__ConfigureOpenElement((Clay_ElementDeclaration) {
            .layout.sizing = { .width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_FIXED(100) },
            .backgroundColor = { 50, 50, 60, 255 },
            .overlayColor = { 0, 0, 0, 100 },
        });
        ui_item_behavior(ID("hov_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        fl_layout_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(hit_clip, an_element_with_an_overlay_color_still_takes_hover) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitoverlay"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_overlay_emit(ctx);
    const u32 item_id = ui_layout_id_to_clay_id(ID("hov_item"));
    Clay_ElementData box = Clay_GetElementData((Clay_ElementId) { .id = item_id });
    ASSERT_TRUE_MSG(box.found, "the overlaid element must lay out");

    ctx->input_state.mouse.x = box.boundingBox.x + box.boundingBox.width * 0.5f;
    ctx->input_state.mouse.y = box.boundingBox.y + box.boundingBox.height * 0.5f;
    hit_overlay_emit(ctx);
    ASSERT_EQ_MSG(item_id, ctx->hot_id, "the pointer over an overlaid element makes it hot");

    ctx->input_state.mouse.x = -10.0f;
    ctx->input_state.mouse.y = -10.0f;
    fl_app_destroy(app);
}

// Opens levels clip containers that do not restrict, then one that restricts to 200x100. Inside that one are a
// further clip container, which is the push past the walk's stack capacity, and after it closes an item tall
// enough to reach past the restricting clip's bottom edge.

static void hit_clip_deep_nest(int levels) {
    if (levels > 0) {
        ui_layout_area_id(ID_INT("hc_deep", (u32)levels), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
            hit_clip_deep_nest(levels - 1);
        }
        return;
    }

    ui_layout_area_id(ID("hc_deep_limit"), .sizing = { sizing_fixed(200), sizing_fixed(100) },
                      .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
        ui_layout_area_id(ID("hc_deep_over"), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true },
                          .placed = { .enabled = true, .offset = { 0.0f, 0.0f } }) {}
        ui_layout_area_id(ID("hc_deep_item"), .sizing = { sizing_fixed(60), sizing_fixed(120) },
                          .color = ui_color(40, 200, 120, 255),
                          .placed = { .enabled = true, .offset = { 20.0f, 40.0f } }) {
            ui_item_behavior(ID("hc_deep_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        }
    }
}

static void hit_clip_deep_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        hit_clip_deep_nest(31); // 31 + the restricting one == UI_HIT_CLIP_STACK_MAX, so the one inside it is dropped
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// Past the clip stack's capacity a push is dropped, and its matching pop cancels the drop without popping a clip
// that is still in force. The software renderer's scissor stack keeps the same balance
// (render.scissor_stack_deep_nesting_balanced).

UTEST(hit_clip, a_push_past_the_stack_capacity_does_not_pop_a_live_clip) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitclipdeep"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_clip_deep_emit(ctx);

    const u32 item_id = ui_layout_id_to_clay_id(ID("hc_deep_item"));
    Clay_ElementData limit
        = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("hc_deep_limit")) });
    Clay_ElementData item = Clay_GetElementData((Clay_ElementId) { .id = item_id });
    ASSERT_TRUE_MSG(limit.found && item.found, "the deep nest must lay out");

    const f32 limit_bottom = limit.boundingBox.y + limit.boundingBox.height;
    const f32 item_bottom = item.boundingBox.y + item.boundingBox.height;
    ASSERT_GT_MSG(item_bottom, limit_bottom, "the item must overflow the restricting clip");
    ASSERT_LT_MSG(item.boundingBox.y, limit_bottom, "and part of it must survive it");

    const f32 x = item.boundingBox.x + item.boundingBox.width * 0.5f;

    ctx->input_state.mouse.x = x;
    ctx->input_state.mouse.y = (item.boundingBox.y + limit_bottom) * 0.5f;
    hit_clip_deep_emit(ctx);
    ASSERT_EQ_MSG(item_id, ctx->hot_id, "control: inside the restricting clip the item is hot at this depth");

    ctx->input_state.mouse.y = (limit_bottom + item_bottom) * 0.5f;
    hit_clip_deep_emit(ctx);
    ASSERT_NE_MSG(item_id, ctx->hot_id,
                  "the dropped over-capacity push must not take the restricting clip off the stack with it");

    fl_app_destroy(app);
}
