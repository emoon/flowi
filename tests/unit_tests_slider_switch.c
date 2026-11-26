// UI switch, slider and scrollbar tests

#include "widgets/common.h"
#include <math.h>

// The switch shares the checkbox's click and activate behavior (it toggles a bool*) and has a 0..1 knob animation
// that eases toward the on or off target. The case uses the FlUiCtx directly with a fixed dt and checks the
// toggle, the returned signal, and that the knob animation moves toward its target.
UTEST(ui_switch, toggles_and_animates) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // ui_scale is 1.0, and a fixed dt makes the animation deterministic.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;
    ctx->delta_time = dt;

    const FlLayoutId sw_id = { .name = FL_S("test_switch"), .id = 0 };
    const u32 sw_clay = ui_layout_id_to_clay_id(sw_id);

    bool value = false;

#define SWITCH_FRAME(sig_out)                                        \
    do {                                                             \
        ui_update_begin(ctx);                                        \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) { \
            (sig_out) = ui_switch(FL_S("test_switch"), &value);      \
        }                                                            \
        ui_update_end(ctx, dt);                                      \
    } while (0)

    FlSignal sig = { 0 };

    // Frame 1: lay out the switch once so its ItemState exists and the knob starts off (0.0).
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;
    SWITCH_FRAME(sig);

    ItemState* item = hashmap_get(&ctx->shared->item_state, sw_clay);
    ASSERT_TRUE_MSG(item != nullptr, "switch registered an interactive item");
    ASSERT_NEAR_MSG((double)item->check_anim_t, 0.0, 1e-6, "knob starts at the off position");

    // Mark the switch hot so the fast tap in the next frame registers as a click. This skips the frames hover
    // takes to propagate (hover -> hot_id -> state.hot).
    item->state.hot = 1;

    // Frame 2: a fast tap (pressed and released in the same frame) on the hot switch toggles the bool on.
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    SWITCH_FRAME(sig);
    ASSERT_TRUE_MSG(sig.clicked, "fast tap on the hot switch returns a clicked signal");
    ASSERT_TRUE_MSG(value, "click toggled the bool on");

    float prev = -1.0f;
    for (int i = 0; i < 90; i++) {
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        SWITCH_FRAME(sig);
        item = hashmap_get(&ctx->shared->item_state, sw_clay);
        ASSERT_GE_MSG((double)item->check_anim_t, (double)prev, "knob animation moves toward on, never backward");
        prev = item->check_anim_t;
    }
    ASSERT_NEAR_MSG((double)item->check_anim_t, 1.0, 1e-3, "knob settles at the on position");

    // A second fast tap toggles the bool back off.
    item = hashmap_get(&ctx->shared->item_state, sw_clay);
    item->state.hot = 1;
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    SWITCH_FRAME(sig);
    ASSERT_TRUE_MSG(sig.clicked, "second fast tap returns a clicked signal");
    ASSERT_FALSE_MSG(value, "second click toggled the bool off");

    item = hashmap_get(&ctx->shared->item_state, sw_clay);
    const float off_start = item->check_anim_t;
    ASSERT_GT_MSG((double)off_start, 0.5, "knob is still near the on position right after toggling off");
    for (int i = 0; i < 90; i++) {
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        SWITCH_FRAME(sig);
    }
    item = hashmap_get(&ctx->shared->item_state, sw_clay);
    ASSERT_LT_MSG((double)item->check_anim_t, (double)off_start, "knob moved back toward off");
    ASSERT_NEAR_MSG((double)item->check_anim_t, 0.0, 1e-3, "knob settles at the off position");

    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

#undef SWITCH_FRAME
}

// Mounting an already-checked toggle (or remounting one after its ItemState retired while a screen was closed)
// starts settled at 1.0 instead of replaying an off-to-on animation; subsequent flips still animate smoothly.
UTEST(ui_switch, mounts_settled_when_initially_on_and_after_reopen) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;
    ctx->delta_time = dt;

    const FlLayoutId sw_id = { .name = FL_S("test_switch_settled"), .id = 0 };
    const FlLayoutId cb_id = { .name = FL_S("test_checkbox_settled"), .id = 0 };
    const u32 sw_clay = ui_layout_id_to_clay_id(sw_id);
    const u32 cb_clay = ui_layout_id_to_clay_id(cb_id);

    bool sw_on = true;
    bool cb_on = true;

#define SETTLED_FRAME(show_toggles)                                  \
    do {                                                             \
        ui_update_begin(ctx);                                        \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) { \
            if (show_toggles) {                                      \
                ui_switch(FL_S("test_switch_settled"), &sw_on);      \
                ui_checkbox(FL_S("test_checkbox_settled"), &cb_on);  \
            }                                                        \
        }                                                            \
        ui_update_end(ctx, dt);                                      \
    } while (0)

    SETTLED_FRAME(true);
    ItemState* sw = hashmap_get(&ctx->shared->item_state, sw_clay);
    ItemState* cb = hashmap_get(&ctx->shared->item_state, cb_clay);
    ASSERT_TRUE_MSG(sw != nullptr && cb != nullptr, "toggles registered interactive items");
    ASSERT_NEAR_MSG((double)sw->check_anim_t, 1.0, 1e-6, "switch starts settled on without animating");
    ASSERT_NEAR_MSG((double)cb->check_anim_t, 1.0, 1e-6, "checkbox starts settled on without animating");

    // Unmount for a frame so retire_stale_items evicts both ItemStates, then reopen the screen.
    SETTLED_FRAME(false);
    ASSERT_TRUE_MSG(hashmap_get(&ctx->shared->item_state, sw_clay) == nullptr, "switch state retired while unmounted");

    SETTLED_FRAME(true);
    sw = hashmap_get(&ctx->shared->item_state, sw_clay);
    cb = hashmap_get(&ctx->shared->item_state, cb_clay);
    ASSERT_TRUE_MSG(sw != nullptr && cb != nullptr, "toggles recreated on reopen");
    ASSERT_NEAR_MSG((double)sw->check_anim_t, 1.0, 1e-6, "reopened switch starts settled on");
    ASSERT_NEAR_MSG((double)cb->check_anim_t, 1.0, 1e-6, "reopened checkbox starts settled on");

    // Changing the value while mounted still eases smoothly rather than snapping.
    sw_on = false;
    cb_on = false;
    SETTLED_FRAME(true);
    sw = hashmap_get(&ctx->shared->item_state, sw_clay);
    cb = hashmap_get(&ctx->shared->item_state, cb_clay);
    ASSERT_GT_MSG((double)sw->check_anim_t, 0.0, "mounted switch eases off rather than snapping");
    ASSERT_LT_MSG((double)sw->check_anim_t, 1.0, "mounted switch moved toward off");
    ASSERT_GT_MSG((double)cb->check_anim_t, 0.0, "mounted checkbox eases off rather than snapping");
    ASSERT_LT_MSG((double)cb->check_anim_t, 1.0, "mounted checkbox moved toward off");

#undef SETTLED_FRAME
}

// One frame's solid rects, captured from the render call. A switch draws exactly three: the hit-test container,
// the track, then the knob. A slider draws exactly four: the hit-test row, then the filled segment, the thumb and the
// unfilled segment (parent before children, in Clay's render order).
typedef struct FillRects {
    FlRect rect[8];
    int count;
} FillRects;

static FillRects s_fill_rects;

// The renderer the fixture installed.
static void (*s_fill_rects_next)(void*, FlSurface*, const FlRenderFrame*);

static void fill_rects_capture(void* user_data, FlSurface* surface, const FlRenderFrame* frame) {
    s_fill_rects.count = 0;
    for (const u8* p = (const u8*)frame->commands; p < (const u8*)frame->commands + frame->bytes;) {
        const FlCmdHeader* h = (const FlCmdHeader*)p;
        if (h->kind == FlCmd_FillRect && s_fill_rects.count < 8) {
            s_fill_rects.rect[s_fill_rects.count++] = ((const FlCmdFillRect*)(h + 1))->rect;
        }
        p += h->byte_size;
    }
    s_fill_rects_next(user_data, surface, frame);
}

// Routes the fixture's renderer through fill_rects_capture. ui_shared_create copied the backend, so the render call to
// intercept is the one the core holds and not the fixture's own struct.
static void fill_rects_install(FlUiCtx* ctx) {
    if (ctx->shared->renderer.render != fill_rects_capture) {
        s_fill_rects_next = ctx->shared->renderer.render;
        ctx->shared->renderer.render = fill_rects_capture;
    }
}

// The track is FlSwitchConfig's track_width x track_height and the knob is the circle that fits inside
// knob_inset. Zero in any of the three keeps the built-in 48x24 pill with its 20px knob. The case checks the
// emitted fill rects, so the numbers in the assertions are the geometry.
UTEST(ui_switch, geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // At ui_scale 1.0 the emitted physical-px rects are the logical geometry the config asks for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    fill_rects_install(ctx);

    // Renders one switch with cfg and value and leaves its three rects in s_fill_rects. Each case takes two
    // frames: the first registers the widget's ItemState, and the second runs with the knob animation set to
    // exactly 0 or 1, which one dt of easing would not give.
    bool value = false;
#define SWITCH_GEOM_FRAME(label_str, ...)                                                                   \
    do {                                                                                                    \
        const FlLayoutId geom_id = { .name = FL_S(label_str), .id = 0 };                                    \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {                                               \
            ItemState* it = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(geom_id));        \
            if (it != nullptr) {                                                                            \
                it->check_anim_t = value ? 1.0f : 0.0f;                                                     \
            }                                                                                               \
            ui_update_begin(ctx);                                                                           \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                                    \
                ui_switch(FL_S(label_str), &value, __VA_ARGS__);                                            \
            }                                                                                               \
            ui_update_end(ctx, dt);                                                                         \
        }                                                                                                   \
        ASSERT_EQ_MSG(s_fill_rects.count, 3, "a switch draws the hit-test container, the track, the knob"); \
    } while (0)

    // Default geometry: a 48x24 pill with a knob 24 - 2*2 = 20 across, 2px in from the track's left edge.
    SWITCH_GEOM_FRAME("geom_default", .track_width = 0.0f);
    FlRect track = s_fill_rects.rect[1];
    FlRect knob = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(track.w, 48.0, "the default track is twice its height wide"),
                       NEAR_CHECK(track.h, 24.0, "the default track is FlSpacing_LG tall"),
                       NEAR_CHECK(knob.w, 20.0, "the default knob is the track height less both insets"),
                       NEAR_CHECK(knob.h, 20.0, "the knob is square (a circle at full radius)"),
                       NEAR_CHECK(knob.x - track.x, 2.0, "the off knob sits one inset in from the left"));

    // track_height alone still derives the width, so a skin that sets only the height keeps the pill's
    // proportion.
    SWITCH_GEOM_FRAME("geom_height_only", .track_height = 13.0f);
    ASSERT_NEAR_CHECKS(NEAR_CHECK(s_fill_rects.rect[1].w, 26.0, "width still derives from the skinned height"),
                       NEAR_CHECK(s_fill_rects.rect[1].h, 13.0, "the skinned height applies"));

    // The compact skin, off: a 26x13 track with a 9px knob.
    SWITCH_GEOM_FRAME("geom_skin", .track_width = 26.0f, .track_height = 13.0f);
    track = s_fill_rects.rect[1];
    knob = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(track.w, 26.0, "the skinned track width applies"),
                       NEAR_CHECK(track.h, 13.0, "the skinned track height applies"),
                       NEAR_CHECK(knob.w, 9.0, "the knob derives as track_height - 2 * knob_inset"),
                       NEAR_CHECK(knob.x - track.x, 2.0, "the off knob sits one inset in from the left"));

    // The same skin fully on: the knob is at x = 15 within the track (26 - 9 - 2).
    value = true;
    SWITCH_GEOM_FRAME("geom_skin", .track_width = 26.0f, .track_height = 13.0f);
    track = s_fill_rects.rect[1];
    knob = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(knob.x - track.x, 15.0, "the on knob stops one inset short of the right edge"));

    value = false;

    // An explicit knob_inset sets the knob size directly: 13 - 2*4 = 5.
    SWITCH_GEOM_FRAME("geom_inset", .track_height = 13.0f, .knob_inset = 4.0f);
    track = s_fill_rects.rect[1];
    knob = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(knob.w, 5.0, "an explicit inset eats into the derived knob size"),
                       NEAR_CHECK(knob.x - track.x, 4.0, "the off knob sits one explicit inset in from the left"));

    // An inset past half the track height leaves no knob. The size does not go negative.
    SWITCH_GEOM_FRAME("geom_fat_inset", .track_height = 13.0f, .knob_inset = 10.0f);
    knob = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(knob.w, 0.0, "an over-large inset collapses the knob instead of inverting it"),
                       NEAR_CHECK(knob.h, 0.0, "an over-large inset collapses the knob instead of inverting it"));

    // A track narrower than its own knob leaves no travel. Without the clamp the travel would be negative, and
    // the knob's left padding is an unsigned pixel count, so the knob would wrap to the far side of the screen.
    value = true;
    SWITCH_GEOM_FRAME("geom_narrow", .track_width = 8.0f, .track_height = 12.0f);
    track = s_fill_rects.rect[1];
    knob = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(knob.x - track.x, 2.0, "a track with no room to travel pins the knob at rest"));

#undef SWITCH_GEOM_FRAME
}

// The thumb is a square of FlSliderConfig's thumb_size, and the track bar is track_height thick and centered
// against that thumb. Zero in either keeps the built-in 24px thumb over an 8px bar. The case checks the emitted
// fill rects, so the numbers in the assertions are the geometry.
UTEST(ui_slider, geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // At ui_scale 1.0 the emitted physical-px rects are the logical geometry the config asks for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    fill_rects_install(ctx);

    // Renders one slider with cfg and leaves its four rects in s_fill_rects. Each case takes two frames: the
    // filled and unfilled widths come from the row width the item kept from the previous frame, so the first
    // frame lays out against a zero-width row and only the second reflects the value.
    float value = 0.5f;
#define SLIDER_GEOM_FRAME(label_str, ...)                                          \
    do {                                                                           \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {                      \
            ui_update_begin(ctx);                                                  \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {           \
                ui_slider_float(FL_S(label_str), &value, 0.0f, 1.0f, __VA_ARGS__); \
            }                                                                      \
            ui_update_end(ctx, dt);                                                \
        }                                                                          \
        ASSERT_EQ_MSG(s_fill_rects.count, 4,                                       \
                      "a slider draws the row, the filled bar, the thumb, the "    \
                      "unfilled bar");                                             \
    } while (0)

    // Default geometry: a 24px thumb over an 8px bar, with the bar centered in the row ((24 - 8) / 2 = 8 down).
    SLIDER_GEOM_FRAME("slider_geom_default", .thumb_size = 0.0f);
    FlRect row = s_fill_rects.rect[0];
    FlRect filled = s_fill_rects.rect[1];
    FlRect thumb = s_fill_rects.rect[2];
    FlRect unfilled = s_fill_rects.rect[3];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(thumb.w, 24.0, "the default thumb is UI_SLIDER_THUMB_WIDTH across"),
                       NEAR_CHECK(thumb.h, 24.0, "the thumb is square (a circle at full radius)"),
                       NEAR_CHECK(row.h, 24.0, "the row is as tall as the thumb"),
                       NEAR_CHECK(filled.h, 8.0, "the default track bar is 8px thick"),
                       NEAR_CHECK(unfilled.h, 8.0, "both track segments share the bar thickness"),
                       NEAR_CHECK(filled.y - row.y, 8.0, "the thin bar centers vertically against the thumb"),
                       // At 0.5 the thumb is at mid travel, so the two segments share the row's width less the thumb
                       // evenly.
                       NEAR_CHECK(filled.w, unfilled.w, "a half-value slider splits its travel evenly"),
                       NEAR_CHECK(filled.w + thumb.w + unfilled.w, row.w, "the three children fill the row exactly"));

    // The compact skin: a 3px bar under a 13px thumb, with the bar centered at (13 - 3) / 2 = 5 down.
    SLIDER_GEOM_FRAME("slider_geom_skin", .track_height = 3.0f, .thumb_size = 13.0f);
    row = s_fill_rects.rect[0];
    filled = s_fill_rects.rect[1];
    thumb = s_fill_rects.rect[2];
    ASSERT_NEAR_CHECKS(NEAR_CHECK(thumb.w, 13.0, "the skinned thumb size applies"),
                       NEAR_CHECK(thumb.h, 13.0, "the skinned thumb stays square"),
                       NEAR_CHECK(row.h, 13.0, "the row height follows the skinned thumb, not the bar"),
                       NEAR_CHECK(filled.h, 3.0, "the skinned track thickness applies"),
                       NEAR_CHECK(filled.y - row.y, 5.0, "the 3px bar centers against the 13px thumb"));

    // Either field alone keeps the other's default.
    SLIDER_GEOM_FRAME("slider_geom_track_only", .track_height = 3.0f);
    ASSERT_NEAR_CHECKS(NEAR_CHECK(s_fill_rects.rect[2].w, 24.0, "the thumb keeps its default beside a thin bar"),
                       NEAR_CHECK(s_fill_rects.rect[1].h, 3.0, "the skinned thickness applies on its own"));
    SLIDER_GEOM_FRAME("slider_geom_thumb_only", .thumb_size = 13.0f);
    ASSERT_NEAR_CHECKS(NEAR_CHECK(s_fill_rects.rect[2].w, 13.0, "the skinned thumb applies on its own"),
                       NEAR_CHECK(s_fill_rects.rect[1].h, 8.0, "the bar keeps its default under a small thumb"));

#undef SLIDER_GEOM_FRAME
}

typedef struct SliderProbe {
    FlString label; // also the widget id, so each thumb size needs its own
    float thumb_size;
    float value;
} SliderProbe;

// Runs one pointer frame, then lays the slider out and lets it consume the click.
static void slider_click_frame(FlUiCtx* ctx, SliderProbe* slider, PointerFrame pointer) {
    pointer_frame_apply(ctx, pointer);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_slider_float(slider->label, &slider->value, 0.0f, 1.0f, .thumb_size = slider->thumb_size);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

typedef struct SliderClick {
    float value; // what the slider wrote
    float row_w; // the row's width, so the caller can write the expected mapping from the geometry
} SliderClick;

// Clicks click_x pixels in from the slider row's left edge.
static SliderClick slider_click_value(FlUiCtx* ctx, FlString label, float thumb_size, float click_x) {
    SliderProbe slider = { .label = label, .thumb_size = thumb_size };

    // Two frames with the cursor parked off the window. The first lays the row out (the drag math divides by the
    // width kept from the previous frame) and the second settles it before any hover.
    const PointerFrame parked = { .x = -100.0f, .y = -100.0f };
    slider_click_frame(ctx, &slider, parked);
    slider_click_frame(ctx, &slider, parked);

    const ItemState* row
        = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = 0 }));
    if (row == nullptr) {
        return (SliderClick) { .value = slider.value, .row_w = 0.0f };
    }
    const float mx = row->state.pos.x + click_x;
    const float my = row->state.pos.y + row->state.size.y / 2.0f;
    const float row_w = row->state.size.x;

    // Hover for two frames: the row becomes hot_id at the end of the first and its state.hot is set on the
    // second, and a press needs signal.hovered. Then press, then release. clicked fires on the release frame.
    const PointerFrame hover = { .x = mx, .y = my };
    slider_click_frame(ctx, &slider, hover);
    slider_click_frame(ctx, &slider, hover);
    slider_click_frame(ctx, &slider, (PointerFrame) { .x = mx, .y = my, .held = true, .pressed = true });
    slider_click_frame(ctx, &slider, (PointerFrame) { .x = mx, .y = my, .released = true });
    return (SliderClick) { .value = slider.value, .row_w = row_w };
}

// Clicking a point on the track maps to a value through the configured thumb size and not the built-in 24px. The
// expected values are written as the mapping: mouse x less half a thumb, over the travel the thumb has.
UTEST(ui_slider, hit_math_follows_thumb_size) {
    FlAppSettings settings = { .window_title = FL_S("slider_hit"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ctx->ui_scale = 1.0f; // logical == physical, so an injected window pixel is the logical x the slider sees
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    // The same click point on both sliders, well inside the track so neither mapping clamps at an end.
    const float click_x = 100.0f;

    const SliderClick clicked_default = slider_click_value(ctx, FL_S("hit_default"), 0.0f, click_x);
    const float got_default = clicked_default.value;
    ASSERT_TRUE_MSG(clicked_default.row_w > 0.0f, "the default slider's row laid out");
    const float want_default
        = (click_x - UI_SLIDER_THUMB_WIDTH / 2.0f) / (clicked_default.row_w - UI_SLIDER_THUMB_WIDTH);
    ASSERT_NEAR_MSG((double)got_default, (double)want_default, 1e-4,
                    "a zero thumb_size keeps the built-in 24px thumb in the click mapping");

    const float thumb = 13.0f;
    const SliderClick clicked_skin = slider_click_value(ctx, FL_S("hit_skin"), thumb, click_x);
    const float got_skin = clicked_skin.value;
    ASSERT_TRUE_MSG(clicked_skin.row_w > 0.0f, "the skinned slider's row laid out");
    const float want_skin = (click_x - thumb / 2.0f) / (clicked_skin.row_w - thumb);
    ASSERT_NEAR_MSG((double)got_skin, (double)want_skin, 1e-4,
                    "the configured thumb_size drives the click mapping, not UI_SLIDER_THUMB_WIDTH");

    // The two mappings differ, so the default math cannot satisfy both assertions above.
    ASSERT_TRUE_MSG(got_skin != got_default, "a smaller thumb maps the same click to a different value");

    fl_app_destroy(app);
}

UTEST(ui_slider, equal_min_and_max_keeps_finite_geometry) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);

    fill_rects_install(ctx);

    float value = 3.0f;
    for_count(pass, 2) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
            ui_slider_float(FL_S("slider_flat_range"), &value, 3.0f, 3.0f);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    ASSERT_EQ_MSG(s_fill_rects.count, 4, "a slider draws the row, the filled bar, the thumb, the unfilled bar");
    const float filled_w = s_fill_rects.rect[1].w;
    const float unfilled_w = s_fill_rects.rect[3].w;
    ASSERT_TRUE_MSG(isfinite(filled_w), "the filled bar has a finite width");
    ASSERT_TRUE_MSG(isfinite(unfilled_w), "the unfilled bar has a finite width");
    ASSERT_NEAR_MSG((double)value, 3.0, 1e-4, "the value stays at the only value in range");
}

static void slider_int_wheel_frame(FlUiCtx* ctx, FlString label, int* value, float wheel_y) {
    const ItemState* row
        = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = 0 }));
    if (row != nullptr) {
        ui_input_set_mouse_position(ctx, row->state.pos.x + 50.0f, row->state.pos.y + row->state.size.y / 2.0f);
    }
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = wheel_y;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_slider_int(label, value, 0, 10, .step = 0.0f);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(ui_slider, int_slider_with_a_small_range_still_moves) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);

    int value = 5;
    const FlString label = FL_S("slider_int_small_range");
    for_count(i, 3) {
        slider_int_wheel_frame(ctx, label, &value, 0.0f);
    }
    slider_int_wheel_frame(ctx, label, &value, 1.0f);
    ASSERT_EQ_MSG(value, 6, "one wheel step moves the integer slider by one");
}

static FlUiCtx* scrollbar_test_ctx(void) {
    FlUiCtx* ctx = g_ui_ctx;
    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });
    return ctx;
}

static void scrollbar_click_frame(FlUiCtx* ctx, float* value, PointerFrame pointer) {
    pointer_frame_apply(ctx, pointer);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(100.0f), sizing_fixed(100.0f) }) {
        ui_scrollbar(value, .min = 0.0f, .max = 100.0f, .min_thumb_size = 200.0f);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(ui_scrollbar, grabbing_a_thumb_that_fills_the_track_keeps_the_value) {
    FlAppSettings settings = { .window_title = FL_S("scrollbar_short"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = scrollbar_test_ctx();

    float value = 50.0f;
    for_count(i, 2) {
        scrollbar_click_frame(ctx, &value, (PointerFrame) { .x = -100.0f, .y = -100.0f });
    }
    const PointerFrame hover = { .x = 4.0f, .y = 50.0f };
    scrollbar_click_frame(ctx, &value, hover);
    scrollbar_click_frame(ctx, &value, hover);
    scrollbar_click_frame(ctx, &value, (PointerFrame) { .x = hover.x, .y = hover.y, .held = true, .pressed = true });
    scrollbar_click_frame(ctx, &value, (PointerFrame) { .x = hover.x, .y = hover.y, .released = true });

    ASSERT_NEAR_MSG((double)value, 50.0, 1e-4, "a press on a thumb that fills the track does not move the value");
    fl_app_destroy(app);
}

static void scrollbar_drag_frame(float* value, float my, bool held, bool pressed) {
    FlUiCtx* ctx = g_ui_ctx;
    pointer_frame_apply(ctx, (PointerFrame) { .x = 4.0f, .y = my, .held = held, .pressed = pressed });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(100.0f), sizing_fixed(100.0f) }) {
        ui_scrollbar(value, .min = 0.0f, .max = 100.0f, .viewport_ratio = 0.2f);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(ui_scrollbar, a_press_on_the_thumb_keeps_the_grabbed_point_under_the_mouse) {
    FlAppSettings settings = { .window_title = FL_S("scrollbar_grab"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    scrollbar_test_ctx();

    float value = 50.0f;
    for_count(i, 3) {
        scrollbar_drag_frame(&value, 45.0f, false, false);
    }
    scrollbar_drag_frame(&value, 45.0f, true, true);
    scrollbar_drag_frame(&value, 45.0f, true, false);
    EXPECT_NEAR_MSG((double)value, 50.0, 1e-3, "a press 15 px below the thumb's top edge does not move the value");

    scrollbar_drag_frame(&value, 53.0f, true, false);
    EXPECT_NEAR_MSG((double)value, 50.0 + 800.0 / 60.0, 1e-3, "8 px of drag on a 60 px usable track");
    fl_app_destroy(app);
}

UTEST(ui_scrollbar, a_press_on_the_track_centres_the_thumb_on_the_mouse) {
    FlAppSettings settings = { .window_title = FL_S("scrollbar_track"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    scrollbar_test_ctx();

    float value = 0.0f;
    for_count(i, 3) {
        scrollbar_drag_frame(&value, 70.0f, false, false);
    }
    scrollbar_drag_frame(&value, 70.0f, true, true);
    scrollbar_drag_frame(&value, 70.0f, true, false);
    EXPECT_NEAR_MSG((double)value, 5000.0 / 60.0, 1e-3, "the thumb's centre lands on the mouse");
    fl_app_destroy(app);
}
