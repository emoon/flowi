// UI selection, scroll math, inspector snapshot and text formatting tests

#include "unit_tests_common.h"
#include <math.h>

// FlSelectionSet membership and applying Selection Requests.

#define IDS(...) (const u64[]) { __VA_ARGS__ }, (sizeof((const u64[]) { __VA_ARGS__ }) / sizeof(u64))

static void expect_members(int* utest_result, FlSelectionSet* s, const u64* ids, size_t count) {
    for (size_t i = 0; i < count; i++) {
        EXPECT_TRUE(selection_set_contains(s, ids[i]));
    }
}

static void expect_non_members(int* utest_result, FlSelectionSet* s, const u64* ids, size_t count) {
    for (size_t i = 0; i < count; i++) {
        EXPECT_FALSE(selection_set_contains(s, ids[i]));
    }
}

UTEST(selection, set_membership) {
    FlSelectionSet* s = selection_set_create(0);
    EXPECT_FALSE(selection_set_contains(s, 42));
    selection_set_set(s, 42, true);
    EXPECT_TRUE(selection_set_contains(s, 42));
    EXPECT_EQ((int)selection_set_len(s), 1);
    selection_set_set(s, 42, true);
    EXPECT_EQ((int)selection_set_len(s), 1);
    selection_set_set(s, 42, false);
    EXPECT_FALSE(selection_set_contains(s, 42));
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_set(s, 7, false);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

UTEST(selection, clear_all_and_set_all_from_iter) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 ids[] = { 10, 20, 30 };
    selection_set_set_all(s, ids, 3);
    EXPECT_EQ((int)selection_set_len(s), 3);
    EXPECT_TRUE(selection_set_contains(s, 10));
    EXPECT_TRUE(selection_set_contains(s, 30));
    const u64 ids2[] = { 99 }; // set_all clears the set, then inserts everything
    selection_set_set_all(s, ids2, 1);
    EXPECT_EQ((int)selection_set_len(s), 1);
    EXPECT_TRUE(selection_set_contains(s, 99));
    EXPECT_FALSE(selection_set_contains(s, 10));
    selection_set_clear_all(s);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

UTEST(selection, copy_ids_snapshot) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 ids[] = { 1, 2, 3, 4 };
    selection_set_set_all(s, ids, 4);
    u64 out[8] = { 0 };
    u64 n = selection_set_copy_ids(s, out, 8);
    EXPECT_EQ((int)n, 4);
    u64 sum = 0; // the order is unspecified, so compare the sum
    for (u64 i = 0; i < n; i++) {
        sum += out[i];
    }
    EXPECT_EQ((int)sum, 10);
    EXPECT_EQ((int)selection_set_copy_ids(s, out, 2), 2);
    EXPECT_EQ((int)selection_set_copy_ids(s, nullptr, 0), 0); // with no buffer it returns 0
    selection_set_destroy(s);
}

UTEST(selection, apply_set_one_no_enumeration_needed) {
    FlSelectionSet* s = selection_set_create(0);
    FlSelectionRequest reqs[] = {
        { .kind = FlSelectionRequestKind_SetOne, .id = 5, .selected = true },
        { .kind = FlSelectionRequestKind_SetOne, .id = 6, .selected = true },
        { .kind = FlSelectionRequestKind_SetOne, .id = 5, .selected = false },
    };
    selection_set_apply(s, reqs, 3, nullptr, 0); // SetOne and ClearAll do not need the ordered list
    EXPECT_FALSE(selection_set_contains(s, 5));
    EXPECT_TRUE(selection_set_contains(s, 6));
    EXPECT_EQ((int)selection_set_len(s), 1);
    selection_set_destroy(s);
}

UTEST(selection, apply_sequence_is_semantics) {
    FlSelectionSet* s = selection_set_create(0);
    selection_set_set(s, 100, true);
    selection_set_set(s, 200, true);
    FlSelectionRequest reqs[] = {
        { .kind = FlSelectionRequestKind_ClearAll },
        { .kind = FlSelectionRequestKind_SetOne, .id = 7, .selected = true },
    };
    selection_set_apply(s, reqs, 2, nullptr, 0);
    EXPECT_EQ((int)selection_set_len(s), 1);
    EXPECT_TRUE(selection_set_contains(s, 7));
    selection_set_destroy(s);
}

UTEST(selection, apply_set_all_resolves_from_ordered_ids) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 11, 22, 33 };
    FlSelectionRequest sel_all = { .kind = FlSelectionRequestKind_SetAll, .selected = true };
    selection_set_apply(s, &sel_all, 1, model, 3);
    EXPECT_EQ((int)selection_set_len(s), 3);
    EXPECT_TRUE(selection_set_contains(s, 22));
    FlSelectionRequest desel_all = { .kind = FlSelectionRequestKind_SetAll, .selected = false };
    selection_set_apply(s, &desel_all, 1, model, 3);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

UTEST(selection, apply_range_spans_offscreen_ids) {
    // apply walks the full ordered model and not the visible window, so a range covers rows a virtualized List
    // never emitted.
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 10, 20, 30, 40, 50 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 20, .to_id = 40, .selected = true };
    selection_set_apply(s, &r, 1, model, 5);
    expect_non_members(utest_result, s, IDS(10, 50));
    expect_members(utest_result, s, IDS(20, 30, 40));
    EXPECT_EQ((int)selection_set_len(s), 3);
    selection_set_destroy(s);
}

UTEST(selection, apply_range_is_direction_agnostic) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 10, 20, 30, 40, 50 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 40, .to_id = 20, .selected = true };
    selection_set_apply(s, &r, 1, model, 5);
    EXPECT_EQ((int)selection_set_len(s), 3);
    expect_members(utest_result, s, IDS(20, 30, 40));
    selection_set_destroy(s);
}

UTEST(selection, apply_range_deselect_and_single_endpoint) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 1, 2, 3, 4, 5 };
    selection_set_set_all(s, model, 5);
    FlSelectionRequest deselect = { .kind = FlSelectionRequestKind_SetRange, .id = 2, .to_id = 4, .selected = false };
    selection_set_apply(s, &deselect, 1, model, 5);
    expect_members(utest_result, s, IDS(1, 5));
    expect_non_members(utest_result, s, IDS(2, 3, 4));
    FlSelectionRequest one = { .kind = FlSelectionRequestKind_SetRange, .id = 3, .to_id = 3, .selected = true };
    selection_set_apply(s, &one, 1, model, 5);
    EXPECT_TRUE(selection_set_contains(s, 3));
    EXPECT_EQ((int)selection_set_len(s), 3); // {1, 3, 5}
    selection_set_destroy(s);
}

UTEST(selection, apply_range_accepts_id_zero) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 0, 1, 2, 3 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 0, .to_id = 2, .selected = true };
    selection_set_apply(s, &r, 1, model, 4);
    EXPECT_EQ((int)selection_set_len(s), 3);
    EXPECT_TRUE(selection_set_contains(s, 0));
    selection_set_destroy(s);
}

#ifdef NDEBUG
UTEST(selection, apply_range_missing_endpoint_is_noop_in_release) {
    // A missing endpoint is an app bug. Debug builds assert, and release builds apply nothing.
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 1, 2, 3 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 2, .to_id = 99, .selected = true };
    selection_set_apply(s, &r, 1, model, 3);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}
#endif

UTEST(selection, apply_rows_reads_ids_from_item_descriptors) {
    FlSelectionSet* s = selection_set_create(0);
    const FlListItem items[] = { { .id = 10 }, { .id = 20 }, { .id = 30 }, { .id = 40 } };
    ListRows rows = list_rows_from_items(items, 4);

    FlSelectionRequest range = { .kind = FlSelectionRequestKind_SetRange, .id = 30, .to_id = 20, .selected = true };
    selection_set_apply_rows(s, &range, 1, &rows);
    EXPECT_EQ((int)selection_set_len(s), 2);
    EXPECT_TRUE(selection_set_contains(s, 20));
    EXPECT_TRUE(selection_set_contains(s, 30));

    FlSelectionRequest all = { .kind = FlSelectionRequestKind_SetAll, .selected = true };
    selection_set_apply_rows(s, &all, 1, &rows);
    EXPECT_EQ((int)selection_set_len(s), 4);

    ListRows none = list_rows_from_items(nullptr, 4);
    selection_set_apply_rows(s, &all, 1, &none);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

// The scroll and virtualization math in ui/scroll_core.c, shared by ui_scroll, list_uniform and content_view.

UTEST(scroll_math, max_never_negative) {
    EXPECT_NEAR(scroll_max(1000.0f, 300.0f), 700.0f, 0.001f);
    const f32 content_not_taller[] = { 200.0f, 300.0f, 0.0f };
    for_count(i, sizeof_array(content_not_taller)) {
        EXPECT_EQ(scroll_max(content_not_taller[i], 300.0f), 0.0f);
    }
}

static void expect_strictly_between(int* utest_result, f32 value, f32 low, f32 high) {
    EXPECT_GT(value, low);
    EXPECT_LT(value, high);
}

UTEST(scroll_math, smooth_step_eases_then_snaps_and_clamps) {
    f32 dt = 1.0f / 60.0f;
    f32 speed = 8.0f;

    f32 o = scroll_smooth_step(0.0f, 100.0f, 1000.0f, speed, dt);
    EXPECT_NEAR(o, 100.0f * (speed * dt), 0.01f); // about 13.33px after one 60fps frame
    expect_strictly_between(utest_result, o, 0.0f, 100.0f);

    // Within half a pixel of the target it snaps to it.
    EXPECT_EQ(scroll_smooth_step(99.7f, 100.0f, 1000.0f, speed, dt), 100.0f);
    // Just beyond that it eases: diff = 0.6 > 0.5.
    f32 near = scroll_smooth_step(100.0f, 100.6f, 1000.0f, speed, dt);
    EXPECT_NEAR(near, 100.0f + 0.6f * (speed * dt), 0.01f);
    expect_strictly_between(utest_result, near, 100.0f, 100.6f);

    EXPECT_EQ(scroll_smooth_step(40.0f, 500.0f, 50.0f, speed, dt), 50.0f); // eased past max, so clamped
    EXPECT_EQ(scroll_smooth_step(10.0f, -100.0f, 50.0f, speed, dt), 0.0f); // a negative target clamps to 0
}

UTEST(scroll_math, smooth_step_is_frame_rate_independent) {
    f32 speed = 8.0f;
    f32 target = 600.0f, max = 1000.0f;

    // From a standstill, one step's progress is exactly target * speed * dt.
    f32 step_120 = scroll_smooth_step(0.0f, target, max, speed, 1.0f / 120.0f);
    f32 step_60 = scroll_smooth_step(0.0f, target, max, speed, 1.0f / 60.0f);
    EXPECT_NEAR(step_120, target * speed / 120.0f, 0.01f); // 40px
    EXPECT_NEAR(step_60, target * speed / 60.0f, 0.01f);   // 80px
    EXPECT_NEAR(step_60, step_120 * 2.0f, 0.01f);

    // Over the same wall-clock duration both frame rates converge on the target.
    f32 lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < 60; i++) {
        lo = scroll_smooth_step(lo, target, max, speed, 1.0f / 60.0f); // 1s at 60fps
    }
    for (int i = 0; i < 30; i++) {
        hi = scroll_smooth_step(hi, target, max, speed, 1.0f / 30.0f); // 1s at 30fps
    }
    EXPECT_EQ(lo, target);
    EXPECT_EQ(hi, target);

    // A dt of 0 makes no progress. A huge dt clamps the lerp factor to 1, which snaps to the target.
    f32 no_time = scroll_smooth_step(10.0f, 200.0f, 1000.0f, speed, 0.0f);
    EXPECT_EQ(no_time, 10.0f);
    f32 huge_dt = scroll_smooth_step(10.0f, 200.0f, 1000.0f, speed, 10.0f);
    EXPECT_EQ(huge_dt, 200.0f);
}

UTEST(scroll_math, visible_range_survives_values_no_integer_holds) {
    ScrollRange r = scroll_visible_range(100.0, 500.0f, 1.0e-30f, 1000, 0);
    EXPECT_EQ(r.start, 1000u);
    EXPECT_EQ(r.end, 1000u);

    r = scroll_visible_range((f64)NAN, 500.0f, 50.0f, 1000, 0);
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.end, 0u);

    r = scroll_visible_range(0.0, 500.0f, NAN, 1000, 0);
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.end, 0u);

    r = scroll_visible_range(0.0, INFINITY, 50.0f, 1000, 0);
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.end, 0u);
}

UTEST(scroll_math, visible_range_virtualization_window) {
    f32 stride = 50.0f; // item height + gap
    f32 viewport = 500.0f;
    u32 buffer = 2;
    u32 total = 100;

    // Scrolled to the top: the buffer cannot push the window below 0.
    ScrollRange r = scroll_visible_range(0.0f, viewport, stride, total, buffer);
    EXPECT_EQ(r.start, 0u);
    // floor(500/50) + 1 + 2 = 13: the row at the viewport's bottom edge plus the trailing buffer.
    EXPECT_EQ(r.end, 13u);

    // Scrolled into the middle: first visible = floor(1000/50) - 2 = 18.
    r = scroll_visible_range(1000.0f, viewport, stride, total, buffer);
    EXPECT_EQ(r.start, 18u);
    EXPECT_EQ(r.end, 33u);

    r = scroll_visible_range(4800.0f, viewport, stride, total, buffer);
    EXPECT_EQ(r.end, total);
    EXPECT_LE(r.start, r.end);

    r = scroll_visible_range(75.0f, 30.0f, stride, total, 0);
    EXPECT_EQ(r.start, 1u);
    EXPECT_EQ(r.end, 3u);

    r = scroll_visible_range(-2000.0f, viewport, stride, total, 0);
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.end, 0u);
    r = scroll_visible_range(9000.0f, viewport, stride, total, 0);
    EXPECT_EQ(r.start, total);
    EXPECT_EQ(r.end, total);

    // A zero stride gives an empty window and does not divide by zero.
    r = scroll_visible_range(100.0f, viewport, 0.0f, total, buffer);
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.end, 0u);

    r = scroll_visible_range(0.0f, viewport, stride, 0, buffer);
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.end, 0u);
}

// The JSON shape fl_inspector_snapshot documents in include/flowi/ui/inspector.h. A consumer parses this text, so
// the whole string is compared.

static void inspector_emit_frame(FlUiCtx* ctx, bool with_elements, FlImageHandle image) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        if (with_elements) {
            ui_layout_area_id(ID("pin_rect"), .sizing = { sizing_fixed(100), sizing_fixed(40) },
                              .color = ui_color(40, 200, 60, 255), .radius = corner_radius(6)) {
                ui_text(FL_S("Hi"));
            }
            ui_layout_area_id(ID("pin_border"), .sizing = { sizing_fixed(60), sizing_fixed(20) },
                              .border = { .color = ui_color(220, 200, 60, 255),
                                          .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } }) {}
            fl_image(image, (FlImageOpts) { .color = ui_color(10, 20, 30, 255) });
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(inspector_snapshot, a_frame_with_no_elements_is_an_empty_array) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    inspector_emit_frame(ctx, false, 0);

    char snapshot[256];
    const int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_STREQ("{\"elements\":[],\"elementCount\":0}", snapshot);
    ASSERT_EQ((int32_t)strlen(snapshot), needed);
}

UTEST(inspector_snapshot, elements_carry_their_documented_fields) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u8 pixels[16] = { 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255 };
    FlImageHandle image = fl_image_create_from_pixels(pixels, 2, 2, FlImageFormat_RGBA);
    ASSERT_NE((FlImageHandle)FL_IMAGE_HANDLE_INVALID, image);
    inspector_emit_frame(ctx, true, image);

    // The ids are hashes, so they are read back. Everything else is the documented text.
    ASSERT_EQ(4, fl_inspector_element_count());
    FlInspectorElement elements[4];
    for (int32_t i = 0; i < 4; i++) {
        ASSERT_TRUE(fl_inspector_element_get(i, &elements[i]));
    }
    UiRenderCommand text_command;
    ASSERT_TRUE(ui_get_render_command(1, &text_command));
    ASSERT_EQ(UiRenderCmd_Text, text_command.type);

    char expected[4096];
    snprintf(expected, sizeof(expected),
             "{\"elements\":["
             "{\"id\":%u,\"name\":\"pin_rect\",\"type\":\"rectangle\","
             "\"boundingBox\":{\"x\":0.00,\"y\":0.00,\"width\":100.00,\"height\":40.00},\"zIndex\":0,"
             "\"backgroundColor\":{\"r\":40,\"g\":200,\"b\":60,\"a\":255},"
             "\"cornerRadius\":{\"topLeft\":6.00,\"topRight\":6.00,\"bottomLeft\":6.00,\"bottomRight\":6.00}},"
             "{\"id\":%u,\"name\":\"\",\"type\":\"text\","
             "\"boundingBox\":{\"x\":%.2f,\"y\":%.2f,\"width\":%.2f,\"height\":%.2f},\"zIndex\":0,"
             "\"text\":\"Hi\",\"textColor\":{\"r\":%u,\"g\":%u,\"b\":%u,\"a\":%u},"
             "\"fontSize\":%d,\"fontId\":%d},"
             "{\"id\":%u,\"name\":\"\",\"type\":\"border\","
             "\"boundingBox\":{\"x\":100.00,\"y\":0.00,\"width\":60.00,\"height\":20.00},\"zIndex\":0,"
             "\"borderColor\":{\"r\":220,\"g\":200,\"b\":60,\"a\":255},"
             "\"cornerRadius\":{\"topLeft\":0.00,\"topRight\":0.00,\"bottomLeft\":0.00,\"bottomRight\":0.00}},"
             "{\"id\":%u,\"name\":\"\",\"type\":\"image\","
             "\"boundingBox\":{\"x\":160.00,\"y\":0.00,\"width\":2.00,\"height\":2.00},\"zIndex\":0,"
             "\"backgroundColor\":{\"r\":10,\"g\":20,\"b\":30,\"a\":255}}"
             "],\"elementCount\":4}",
             elements[0].id, elements[1].id, (double)elements[1].x, (double)elements[1].y, (double)elements[1].width,
             (double)elements[1].height, (unsigned)text_command.text_color.r, (unsigned)text_command.text_color.g,
             (unsigned)text_command.text_color.b, (unsigned)text_command.text_color.a, (int)text_command.font_size,
             (int)text_command.font_id, elements[2].id, elements[3].id);

    char snapshot[4096];
    const int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_STREQ(expected, snapshot);
    ASSERT_EQ((int32_t)strlen(expected), needed);

    image_free((ImageHandle)image);
}

// The character filter and grow helpers in ui/ui_text_buffer.h, shared by the text-input widget and the virtual
// keyboard so both apply the same newline policy.

UTEST(text_buffer, empty_filter_permits_control_whitespace) {
    // With no allow-list, '\n', '\r' and '\t' are permitted.
    FlString no_filter = (FlString) { .data = nullptr, .length = 0 };
    const char permitted[] = { '\n', '\r', '\t', 'A', ' ' };
    for (size_t i = 0; i < sizeof(permitted); i++) {
        EXPECT_TRUE(text_buffer_filter_char(permitted[i], no_filter));
    }

    const char rejected[] = { '\b', (char)0x1b }; // backspace and ESC
    for (size_t i = 0; i < sizeof(rejected); i++) {
        EXPECT_FALSE(text_buffer_filter_char(rejected[i], no_filter));
    }
}

UTEST(text_buffer, explicit_allow_list_is_exact) {
    // A non-empty allow-list is an exact set. The newline rule does not admit '\n', '\r' or '\t' again.
    FlString digits = (FlString) { .data = "0123456789", .length = 10 };
    EXPECT_TRUE(text_buffer_filter_char('5', digits));
    const char outside_set[] = { 'a', '\n', '\t' };
    for (size_t i = 0; i < sizeof(outside_set); i++) {
        EXPECT_FALSE(text_buffer_filter_char(outside_set[i], digits));
    }
}

UTEST(text_buffer, grow_doubles_with_floor_and_preserves) {
    FlArena* arena = arena_new();

    char* buffer = arena_alloc_array(arena, char, 4);
    buffer[0] = 'h';
    buffer[1] = 'i';
    u32 capacity = 4;
    u32 length = 2;

    // Growing past the current capacity uses a 256-byte floor, since doubling 4 would still be tiny.
    text_buffer_grow(arena, &buffer, &capacity, length, capacity + 1);
    EXPECT_EQ(capacity, 256u);
    EXPECT_EQ(buffer[0], 'h');
    EXPECT_EQ(buffer[1], 'i');

    char* before = buffer;
    text_buffer_grow(arena, &buffer, &capacity, length, 100);
    EXPECT_EQ(capacity, 256u);
    EXPECT_EQ_MSG(buffer, before, "grow must be a no-op when capacity already suffices");

    text_buffer_grow(arena, &buffer, &capacity, length, 1000);
    EXPECT_EQ(capacity, 1000u);
    EXPECT_EQ(buffer[0], 'h');
    EXPECT_EQ(buffer[1], 'i');

    fl_arena_destroy(arena);
}

// ui_slider_format_value (ui/ui.c) turns a slider's value and printf format into its display string. The int
// path applies the format to an integer, so a %d-style format never reaches a float formatter. A nullptr format
// prints a bare integer, not "5.00".

UTEST(slider_format, int_arbitrary_formats) {
    FlString a = ui_slider_format_value(s_arena, "%d", 5.0f, true);
    ASSERT_TRUE(fl_string_equals(a, FL_S("5")));

    // A width and padding format. Passing a float to it would be undefined behavior.
    FlString b = ui_slider_format_value(s_arena, "%3d", 5.0f, true);
    ASSERT_TRUE(fl_string_equals(b, FL_S("  5")));

    FlString c = ui_slider_format_value(s_arena, "%d ms", 42.0f, true);
    ASSERT_TRUE(fl_string_equals(c, FL_S("42 ms")));

    // The value is rounded to the nearest integer before formatting.
    FlString d = ui_slider_format_value(s_arena, "%d", 2.6f, true);
    ASSERT_TRUE(fl_string_equals(d, FL_S("3")));
}

UTEST(slider_format, int_null_and_empty_format) {
    FlString n = ui_slider_format_value(s_arena, nullptr, 5.0f, true);
    ASSERT_TRUE(fl_string_equals(n, FL_S("5")));

    // An empty format gives an empty string.
    FlString e = ui_slider_format_value(s_arena, "", 5.0f, true);
    ASSERT_TRUE(e.length == 0);
}
