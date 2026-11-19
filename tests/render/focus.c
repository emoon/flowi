// focus order, list virtualization and tab bar tests

#include "render/common.h"

// FlItemFlags_auto_focus is the flag form of FlWidgetConfig.auto_focus. There are two focusable elements in
// order: A, plain focusable, then B, focusable with auto_focus. Auto-restore picks the first focusable element
// when nothing is focused, so B ending up focused can only come from auto_focus.

UTEST(render, item_behavior_auto_focus) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Clear any focus left by an earlier user of the fixture, so auto-restore starts with nothing focused.
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id_a = ID("af_plain");
    const FlLayoutId id_b = ID("af_auto");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // The fill makes each area emit a render command carrying its id, so its interaction state (found,
        // focused) is updated in ui_update_end.
        ui_layout_area_id(id_a, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(40, 40, 60, 255)) {
            fl_item_behavior(id_a, FlItemFlags_focusable);
        }
        ui_layout_area_id(id_b, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(60, 40, 40, 255)) {
            fl_item_behavior(id_b, FlItemFlags_focusable | FlItemFlags_auto_focus);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState sa = ui_get_element_state(id_a);
    FlElementState sb = ui_get_element_state(id_b);
    ASSERT_TRUE_MSG(sa.found && sb.found, "both focusable elements are laid out");
    ASSERT_TRUE_MSG(sb.focused, "auto_focus moves focus to the requesting element");
    ASSERT_FALSE_MSG(sa.focused, "the first focusable element is not focused — auto_focus, not auto-restore, won");
}

// A spacer's extent is authored in logical px like every other length, so it follows ui_scale. The check is a
// fit-width row holding one horizontal spacer. Fit takes the spacer's own width, so the row's laid-out rect is
// the spacer's resolved extent. The case runs at scale 2 because at scale 1 an unscaled spacer looks the same as
// a scaled one.
UTEST(render, a_spacer_follows_the_ui_scale) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(2.0f, 2.0f);

    const FlLayoutId row = ID("spacer_scale_row");
    const FlLayoutId col = ID("spacer_scale_col");

    // Two frames: ui_update_end publishes element state, so the second frame is the first that can read the
    // first's rects.
    for (int i = 0; i < 2; i++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            // The fill makes each box emit a render command carrying its id, which gives ui_get_element_state a
            // rect to report.
            ui_layout_area_id(row, .sizing = { sizing_fit(), sizing_fixed(10) }, .color = ui_color(40, 40, 60, 255)) {
                fl_item_behavior(row, FlItemFlags_clickable);
                fl_spacer_h(10.0f);
            }
            ui_layout_area_id(col, .sizing = { sizing_fixed(10), sizing_fit() },
                              .direction = FlLayoutDirection_TopToBottom, .color = ui_color(40, 40, 60, 255)) {
                fl_item_behavior(col, FlItemFlags_clickable);
                fl_spacer(10.0f);
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    FlElementState h = ui_get_element_state(row);
    FlElementState v = ui_get_element_state(col);

    ASSERT_TRUE_MSG(h.found && v.found, "both spacer holders are laid out");
    ASSERT_EQ_MSG((int)h.size.x, 20, "a 10px horizontal spacer is 20 physical px at scale 2");
    ASSERT_EQ_MSG((int)v.size.y, 20, "a 10px vertical spacer is 20 physical px at scale 2");
}

// The single-line text-overflow mode (FlTextConfig.overflow). A ten-glyph run is placed twice inside the same
// narrow fixed-width box, once per mode, and read from the debug render-command snapshot. The built-in 8x8 font
// is used so every ASCII glyph advances exactly 16px (monospace and no FreeType, so the widths are exact). That
// font has no glyph for U+2026, so the ellipsis measures 0px here, and the test checks that the ellipsis bytes
// are present and not that it reserves width.

UTEST(render, text_overflow_clip_vs_elide) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(20, 20, 24);

    const u16 glyph_px = 16; // built-in 8x8 font at scale 2
    render_use_default_font(ctx, 2);

    // A single ten-glyph word with no spaces, so it is one line: 10 * 16 = 160px unwrapped.
    const FlString name = { .data = "AAAAAAAAAA", .length = 10, .is_static = 1 };
    const float box_w = 64.0f; // 4 glyphs (64px) fit and the 5th (80px) does not
    const FlColor white = { 240, 240, 240, 255 };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area(.sizing = { sizing_fixed(box_w), sizing_fixed(40) }) {
            ui_text_styled(name, .color = white, .no_wrap = true, .overflow = FlOverflow_Clip);
        }
        // The text has no sizing declaration of its own. Elide implies the constraint to the box width.
        ui_layout_area(.sizing = { sizing_fixed(box_w), sizing_fixed(40) }) {
            ui_text_styled(name, .color = white, .no_wrap = true, .overflow = FlOverflow_Elide);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    static const char ellipsis[3] = { (char)0xE2, (char)0x80, (char)0xA6 }; // U+2026
    bool found_clip = false, found_elide = false;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!render_cmd_at_of_type(i, UiRenderCmd_Text, &cmd) || cmd.text.data == nullptr) {
            continue;
        }
        const char* s = (const char*)cmd.text.data;
        const u64 len = cmd.text.length;
        const bool has_ellipsis = len >= 3 && memcmp(s + (len - 3), ellipsis, 3) == 0;
        if (has_ellipsis) {
            found_elide = true;
            const u64 prefix_len = len - 3;
            ASSERT_EQ_MSG((int)prefix_len, 4, "elide keeps the 4 glyphs (64px) that fit the 64px box");
            ASSERT_TRUE_MSG(cmd.width <= box_w + 0.5f, "elided run fits within the box width");
        } else {
            found_clip = true;
            ASSERT_EQ_MSG((int)len, (int)name.length, "clip keeps the full run (no truncation)");
            ASSERT_TRUE_MSG(cmd.width > box_w + 0.5f, "clip run keeps its full width and overflows the box");
            ASSERT_TRUE_MSG(fabsf(cmd.width - (float)(name.length * glyph_px)) < 0.5f,
                            "clip run width is the full unwrapped width");
        }
    }
    ASSERT_TRUE_MSG(found_clip, "expected a clip text command");
    ASSERT_TRUE_MSG(found_elide, "expected an elide text command");
}

// The List foundation (fl_list_begin, _item and _end on list_uniform's core). The case renders a 100-row list
// inside a fixed 240px viewport and checks that virtualization emits only the visible window. The first frame
// establishes the list container height, which list_uniform reads from the previous frame's Clay layout. The
// second frame, with the viewport known, is the one asserted. Each visible row emits one solid rect of a known
// color, so the number of rects of that color is the number of rows rendered.

// Counts the UiRenderCmd_Rectangle commands whose background matches c. UiColor4 has float channels.
static u8 rounded_channel(float v) {
    return (u8)(v + 0.5f);
}

static bool rect_rgb_is(const UiRenderCommand* cmd, FlColor c) {
    return rounded_channel(cmd->bg_color.r) == c.r && rounded_channel(cmd->bg_color.g) == c.g
           && rounded_channel(cmd->bg_color.b) == c.b;
}

static bool rect_cmd_has_color(int index, FlColor c) {
    UiRenderCommand cmd;
    if (!ui_get_render_command(index, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
        return false;
    }
    return rect_rgb_is(&cmd, c) && rounded_channel(cmd.bg_color.a) == c.a;
}

static int list_count_row_rects(FlColor c) {
    int count = 0;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        count += rect_cmd_has_color(i, c) ? 1 : 0;
    }
    return count;
}

// Emits one frame of the test list with the cursor parked off the list, so idle frames report no hover or click.
// Returns the visible range. The per-frame result and whether any row reported a click come back through the
// out-params.
typedef struct ListFrame {
    u32 total;
    FlColor row_color;
    FlListResult* out_result;
    bool* out_any_clicked;
} ListFrame;

static FlListRange list_render_frame(FlUiCtx* ctx, const ListFrame* frame) {
    FlListRange range = { 0, 0, 0 };
    FlListResult result = { 0, 0 };
    bool any_clicked = false;
    // The cursor is parked well off the list, so no row is hovered or clicked.
    render_mouse_away(ctx);
    // A fixed clear color.
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);
    ui_update_begin(ctx);
    // A fixed-size viewport, so the list's grow container has a known size after frame 1.
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
        // A nullptr set means no selection, since this case covers only virtualization, and ordered_ids is not
        // used then.
        range = fl_list_begin(ID("vlist"), nullptr, frame->total, nullptr,
                              (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
        for (u32 i = range.start; i < range.end; i++) {
            // A flat static list can pass the index as the stable id.
            FlSignal sig = fl_list_item_begin((u64)(i + 1));
            any_clicked |= sig.clicked;
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }, .color = frame->row_color) {}
            fl_list_item_end();
        }
        result = fl_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    if (frame->out_result != nullptr) {
        *frame->out_result = result;
    }
    if (frame->out_any_clicked != nullptr) {
        *frame->out_any_clicked = any_clicked;
    }
    return range;
}

static void check_visible_range(int* utest_result, const FlListRange* range, u32 total, FlColor row_color) {
    ASSERT_EQ_MSG(0u, range->start, "list should be scrolled to the top");
    ASSERT_EQ_MSG(total, range->total, "range.total should report the full item count");
    ASSERT_TRUE_MSG(range->end > range->start, "expected a non-empty visible range");
    ASSERT_TRUE_MSG(range->end < total, "virtualization should not emit all rows for a 100-row list");

    const int rendered = list_count_row_rects(row_color);
    ASSERT_EQ_MSG((int)(range->end - range->start), rendered,
                  "rendered row count must equal the visible range (no off-screen rows drawn)");
}

static void check_idle_list_result(int* utest_result, bool any_clicked, const FlListResult* result) {
    ASSERT_TRUE_MSG(!any_clicked, "no input injected — no row should report a click");
    ASSERT_EQ_MSG((u64)0, result->activated_id, "idle frame: no row activated");
    ASSERT_EQ_MSG((u64)0, result->focused_id, "cursor off-list: no row focused");
}

UTEST(render, list_virtualization) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 320.0f, 256.0f);

    const u32 total = 100;
    const FlColor row_color = ui_color(60, 140, 220, 255);

    // Frame 1 establishes the list container height for the virtualization math.
    list_render_frame(ctx, &(ListFrame) { .total = total, .row_color = row_color });
    // Frame 2 has the viewport and is the one asserted.
    FlListResult result;
    bool any_clicked = true;
    const FlListRange range = list_render_frame(
        ctx, &(ListFrame) { total, row_color, .out_result = &result, .out_any_clicked = &any_clicked });

    check_visible_range(utest_result, &range, total, row_color);
    check_idle_list_result(utest_result, any_clicked, &result);

    FlPixelReadback buf;
    read_frame(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    dump_frame("list", &buf);
}

UTEST(render, list_empty) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 320.0f, 256.0f);

    const FlColor row_color = ui_color(60, 140, 220, 255);
    FlListResult result;
    FlListRange range
        = list_render_frame(ctx, &(ListFrame) { .total = 0, .row_color = row_color, .out_result = &result });

    const u32 range_got[3] = { range.start, range.end, range.total };
    const char* const range_msgs[3]
        = { "empty list start should be 0", "empty list should have no visible rows", "empty list total should be 0" };
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ_MSG(0u, range_got[i], range_msgs[i]);
    }
    ASSERT_EQ_MSG(0, list_count_row_rects(row_color), "empty list should emit no row rects");
    ASSERT_EQ_MSG((u64)0, result.activated_id, "empty list: no activation");
    ASSERT_EQ_MSG((u64)0, result.focused_id, "empty list: no focus");
}

// The result is keyed on the app's opaque u64 stable id and never on the dense emission position. The cursor is
// aimed at the row at position 2, and focused_id comes back as that row's stable id.

// The rect of the row whose color is (red, 120, 200), as its center, or false when it is not in the frame.
static bool find_row_center(u8 red, f32* out_x, f32* out_y) {
    const FlColor want = ui_color(red, 120, 200, 255);
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (render_cmd_at_of_type(i, UiRenderCmd_Rectangle, &cmd) && rect_rgb_is(&cmd, want)) {
            *out_x = cmd.x + cmd.width * 0.5f;
            *out_y = cmd.y + cmd.height * 0.5f;
            return true;
        }
    }
    return false;
}

// One frame of a 50-row list whose row at position i has stable id i*10 + 1000 and its own red channel.
static FlListResult emit_split_list_frame(FlUiCtx* ctx, u32 total, f32 hover_x, f32 hover_y) {
    FlListResult result = { 0, 0 };
    ctx->input_state.mouse.x = hover_x;
    ctx->input_state.mouse.y = hover_y;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
        FlListRange r = fl_list_begin(ID("split_list"), nullptr, total, nullptr,
                                      (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
        for (u32 i = r.start; i < r.end; i++) {
            fl_list_item_begin((u64)i * 10ull + 1000ull);
            // Each row has its own red channel, so the target row's rect can be found in the command stream.
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) },
                           .color = ui_color((u8)(20 + i), 120, 200, 255)) {}
            fl_list_item_end();
        }
        result = fl_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return result;
}

UTEST(render, list_stable_id_split) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 320.0f, 256.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    const u32 total = 50;
    const u32 target_pos = 2;
    // The stable id is id(pos) = pos*10 + 1000, which never equals the position, so reporting one in place of
    // the other is caught.
    const u64 target_id = (u64)target_pos * 10ull + 1000ull;

    // Frame 0 parks the cursor off the list and finds the target row. The remaining frames hold the cursor on it.
    // Hover propagates over two frames (item_state_update sets next_hot_id -> hot_id at the end of one frame,
    // item->state.hot follows hot_id at the end of the next, and item_behavior reads it the frame after), so the
    // cursor stays a few frames before the row reports hovered and focused.
    f32 hover_x = -100.0f, hover_y = -100.0f;
    FlListResult result = emit_split_list_frame(ctx, total, hover_x, hover_y);

    // Find the target row's rect by its red channel and aim the cursor at its center.
    ASSERT_TRUE_MSG(find_row_center((u8)(20 + target_pos), &hover_x, &hover_y) && hover_y > 0.0f,
                    "could not locate the target row's rect after frame 1");
    for (int f = 1; f < 4; f++) {
        result = emit_split_list_frame(ctx, total, hover_x, hover_y);
    }

    ASSERT_EQ_MSG(target_id, result.focused_id, "focused_id must report the stable id of the hovered row");
    ASSERT_TRUE_MSG(result.focused_id != (u64)target_pos, "focused_id must not be the dense position");
}

// Adjacent multi-item widgets lay out in one frame without an id collision. A collision makes the debug
// duplicate-id tracker (ui_open_element_with_id) assert fatally, so reaching the end of the frame shows the ids
// are distinct.

UTEST(render, adjacent_radio_groups_no_id_collision) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(g_ui_ctx == ctx, "widgets forward to the current ctx; fixture ctx must be current");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    render_use_default_font(ctx, 3);

    // The int fields are adjacent, so &sel.b == &sel.a + 4. Group A has 5 options, so with a plain linear seed
    // its top item index (4) would land exactly on group B's key.
    struct {
        int a;
        int b;
    } sel = { 0, 0 };
    FlString opts_a[] = { FL_S("A0"), FL_S("A1"), FL_S("A2"), FL_S("A3"), FL_S("A4") };
    FlString opts_b[] = { FL_S("B0"), FL_S("B1"), FL_S("B2") };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_radio_group(opts_a, 5, &sel.a);
        fl_radio_group(opts_b, 3, &sel.b);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Reaching this line means the duplicate-id assert did not fire. With no input injected, the selections are
    // unchanged.
    ASSERT_EQ_MSG(sel.a, 0, "group A selection unchanged with no input");
    ASSERT_EQ_MSG(sel.b, 0, "group B selection unchanged with no input");
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "adjacent radio groups must emit render commands");
}

// The same for tab strips. Two tab bars are keyed on adjacent int fields (a 4-byte gap). The first has 5 tabs, so
// with a plain linear seed its top tab index (4) would alias the second bar's tab 0. Reaching the end of the
// frame without the duplicate-id assert shows the ids are distinct.

UTEST(render, adjacent_tab_bars_no_id_collision) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(g_ui_ctx == ctx, "widgets forward to the current ctx; fixture ctx must be current");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    render_use_default_font(ctx, 3);

    struct {
        int a;
        int b;
    } sel = { 0, 0 };
    FlString tabs_a[] = { FL_S("A0"), FL_S("A1"), FL_S("A2"), FL_S("A3"), FL_S("A4") };
    FlString tabs_b[] = { FL_S("B0"), FL_S("B1"), FL_S("B2") };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_tab_bar(tabs_a, 5, &sel.a);
        ui_tab_bar(tabs_b, 3, &sel.b);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(sel.a, 0, "bar A selection unchanged with no input");
    ASSERT_EQ_MSG(sel.b, 0, "bar B selection unchanged with no input");
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "adjacent tab bars must emit render commands");
}

// Covers the two places in ui_tab_bar_impl that seed ids for keyboard nav (previous and next tab). A mismatch
// there would not trip the duplicate-id assert, since only the render pass opens elements. It would point
// fl_set_focus at the wrong id without any error.

UTEST(render, tab_bar_keyboard_nav_uses_seeded_ids) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    render_use_default_font(ctx, 3);

    int sel = 1;
    FlString tabs[] = { FL_S("T0"), FL_S("T1"), FL_S("T2") };
    const u32 key = (u32)(uintptr_t)&sel;
    const u32 tab0 = ui_item_id_seed(key, 0u);
    const u32 tab1 = ui_item_id_seed(key, 1u);
    const u32 tab2 = ui_item_id_seed(key, 2u);
    const u32 tab0_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = tab0 });
    const u32 tab1_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = tab1 });
    const u32 tab2_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = tab2 });

    // Frame 1 lays out a focusable tab bar so the per-tab ItemStates exist.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_Yes);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ItemState* item1 = hashmap_get(&ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(item1 != nullptr && item1->state.size.x > 0.0f, "middle tab was not laid out");

    ctx->focused_id = tab1_id;
    item1->state.focused_t = 1.0f;
    ui_update_begin(ctx);
    ctx->nav_frame.nav_right = true; // set after begin. The tab bar consumes it before core spatial nav in update_end
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_Yes);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), tab2_id, "nav-right from tab 1 must focus tab 2 (next-tab seed site)");

    item1 = hashmap_get(&ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(item1 != nullptr, "middle tab state persists");
    ctx->focused_id = tab1_id;
    item1->state.focused_t = 1.0f;
    ui_update_begin(ctx);
    ctx->nav_frame.nav_left = true;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_Yes);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), tab0_id, "nav-left from tab 1 must focus tab 0 (prev-tab seed site)");

    ctx->focused_id = 0;
    ctx->nav_frame.nav_left = false;
    ctx->nav_frame.nav_right = false;
}

// The focus ring is animated, so a single frame cannot show it: the case runs several and checks that focused_t
// stays 0 throughout.
//
// A zeroed config carries FlFocusable_Default. If Default removed the focusable flag as No does, every widget
// that never set the field would lose keyboard access. A slider's whole arrow-key adjustment, for one, depends
// on signal.focused.

// The flags of the item state a widget id registered this frame, or false when it has none. Called inside the frame:
// ui_update_end removes every ItemState the frame did not touch (remove_deleted_items), so a lookup after it could
// find the state already gone.
static bool item_flags_by_name(FlUiCtx* ctx, FlString name, FlItemFlags* out_flags) {
    const ItemState* s = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id((FlLayoutId) { .name = name }));
    if (s == nullptr) {
        return false;
    }
    *out_flags = s->flags;
    return true;
}

// The same strip with the field never set: Default leaves the family's focus behaviour alone. Auto-restore only
// repairs focus on a vanished widget, so the stale id makes it run and land on the strip's first entry.
static void check_default_focusable_tab_bar(int* utest_result, FlUiCtx* ctx, FlString* tabs, int* sel) {
    const u32 tab0_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed((u32)(uintptr_t)sel, 0u) });
    ctx->focused_id = 0xBADF0CU;
    ctx->last_focused_id = 0;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, sel);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), tab0_id, "a zeroed config (FlFocusable_Default) keeps the tab order it had");
}

// The widgets whose keyboard interaction depends on focus: a button and a slider with zeroed configs still register
// as focusable.
static void check_zero_config_button_and_slider(int* utest_result, FlUiCtx* ctx) {
    ctx->focused_id = 0xBADF0CU;
    ctx->last_focused_id = 0;
    static float slider_value;
    slider_value = 0.5f;
    const u32 button_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("def_button"), .id = 0 });

    FlItemFlags slider_flags = 0;
    bool slider_found = false;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_button(FL_S("def_button"));
        ui_slider_float(FL_S("def_slider"), &slider_value, 0.0f, 1.0f);
        slider_found = item_flags_by_name(ctx, FL_S("def_slider"), &slider_flags);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), button_id, "a zero-config button is still the first entry in the focus order");
    ASSERT_TRUE_MSG(slider_found, "zero-config slider registered an item state");
    ASSERT_TRUE_MSG((slider_flags & FlItemFlags_focusable) != 0,
                    "a zero-config slider keeps FlItemFlags_focusable — its arrow-key path needs signal.focused");
}

// The scrollbar is the one family that reads FlFocusable_Yes, and the only place Yes has any effect. Everywhere else
// STANDARD_WIDGET_FLAGS already has the bit.
static void check_scrollbar_focusable_flags(int* utest_result, FlUiCtx* ctx) {
    static float bar_value;
    bar_value = 0.0f;
    FlItemFlags flags[3] = { 0 };
    bool found[3] = { false };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_scrollbar(&bar_value, .max = 100.0f, .widget.id_override = FL_S("sbar_default"));
        ui_scrollbar(&bar_value, .max = 100.0f, .widget.id_override = FL_S("sbar_yes"),
                     .widget.focusable = FlFocusable_Yes);
        // The No bar tells == FlFocusable_Yes from a truthiness test: No is 2, so a truthiness test would make
        // this one focusable.
        ui_scrollbar(&bar_value, .max = 100.0f, .widget.id_override = FL_S("sbar_no"),
                     .widget.focusable = FlFocusable_No);
        found[0] = item_flags_by_name(ctx, FL_S("sbar_default"), &flags[0]);
        found[1] = item_flags_by_name(ctx, FL_S("sbar_yes"), &flags[1]);
        found[2] = item_flags_by_name(ctx, FL_S("sbar_no"), &flags[2]);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(found[0] && found[1] && found[2], "all three scrollbars registered an item state");
    ASSERT_TRUE_MSG((flags[0] & FlItemFlags_focusable) == 0,
                    "a zero-config scrollbar stays out of the tab order — it is pointer-driven by default");
    ASSERT_TRUE_MSG((flags[1] & FlItemFlags_focusable) != 0,
                    "FlFocusable_Yes opts the scrollbar in, the one place Yes changes anything");
    ASSERT_TRUE_MSG((flags[2] & FlItemFlags_focusable) == 0,
                    "FlFocusable_No leaves the scrollbar out — a truthiness test would opt it in, No being 2");
}

UTEST(render, focusable_no_stays_out_of_focus_order) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    render_use_default_font(ctx, 3);

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    // A strip's per-tab ids depend only on the selection pointer and not on the labels, so a stack local here
    // would reuse the slot of the tab-bar case above, with its ids and their leftover focused_t. A static has an
    // address no other case can hold.
    static int sel;
    sel = 0;
    FlString tabs[] = { FL_S("N0"), FL_S("N1"), FL_S("N2") };
    const u32 key = (u32)(uintptr_t)&sel;
    const u32 tab0_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed(key, 0u) });

    for (int frame = 0; frame < 4; frame++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_No);
        }
        ui_update_end(ctx, 1.0f / 60.0f);

        ASSERT_EQ_MSG(fl_focused_id(), 0u, "an FlFocusable_No widget must never take auto-restored focus");
        ItemState* tab0 = hashmap_get(&ctx->shared->item_state, tab0_id);
        ASSERT_TRUE_MSG(tab0 != nullptr, "first tab was laid out");
        ASSERT_LT_MSG(tab0->state.focused_t, 0.001f, "its focus ring must never ramp");
    }

    check_default_focusable_tab_bar(utest_result, ctx, tabs, &sel);
    check_zero_config_button_and_slider(utest_result, ctx);
    check_scrollbar_focusable_flags(utest_result, ctx);

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
}

// The strip checks cfg.widget.focusable for its own arrow-key handling (ui_tab_bar.c), separately from the
// focus_order check in ui_item_behavior, so a zeroed config has to count as navigable there too.

UTEST(render, focusable_default_keeps_tab_bar_arrow_nav) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    render_use_default_font(ctx, 3);

    // Static for the same reason as in the case above: per-tab ids depend only on this pointer.
    static int sel;
    sel = 1;
    FlString tabs[] = { FL_S("D0"), FL_S("D1"), FL_S("D2") };
    const u32 key = (u32)(uintptr_t)&sel;
    const u32 tab1_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed(key, 1u) });
    const u32 tab2_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed(key, 2u) });

    // Frame 1 lays the strip out, with the field never set, so the per-tab ItemStates exist.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ItemState* item1 = hashmap_get(&ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(item1 != nullptr && item1->state.size.x > 0.0f, "middle tab was laid out");

    // The check is that the strip consumed the key, and not only where focus ended up. Core spatial navigation
    // would also move tab1 -> tab2 on nav-right, since they are adjacent, so the resulting focus is the same
    // whether or not the strip handled the key. Only the strip clears nav_frame.nav_right, to keep spatial nav
    // from acting as well, and it does so before ui_update_end reads the flag. Reading the flag inside the frame
    // therefore tells the two apart.
    bool consumed_by_strip = false;
    ctx->focused_id = tab1_id;
    item1->state.focused_t = 1.0f;
    ui_update_begin(ctx);
    ctx->nav_frame.nav_right = true; // set after begin. The strip consumes it before core spatial nav
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel);
        consumed_by_strip = !ctx->nav_frame.nav_right;
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_TRUE_MSG(consumed_by_strip, "a zero-config strip handles nav-right itself and consumes it");
    ASSERT_EQ_MSG(fl_focused_id(), tab2_id, "a zero-config strip still moves focus on nav-right");

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
    ctx->nav_frame.nav_right = false;
    ctx->nav_frame.nav_left = false;
}
