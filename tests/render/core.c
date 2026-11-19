// render smoke, inspector and layout scope tests

#include "render/common.h"

// The whole pipeline end to end, with no display, SDL or plugins.

UTEST(render, smoke) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // The layout runs at a small logical size. The software renderer is the fixture's fixed 1920x1080 offscreen
    // RGB24 buffer.
    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    // A red that is easy to tell from the dark clear color.
    const FlColor rect_color = ui_color(200, 40, 50, 255);

    // ui_update_end() rasterizes into the offscreen buffer.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = rect_color) {}
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame(utest_result, ctx, &buf);
    RETURN_IF_FAILED();
    ASSERT_TRUE_MSG(buf.width > 0 && buf.height > 0, "render buffer has zero size");

    // A pixel well inside the rendered rectangle.
    const int px = 16;
    const int py = 16;
    ASSERT_TRUE_MSG(px < (int)buf.width && py < (int)buf.height && px < layout_w && py < layout_h,
                    "sample point out of range");

    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)py * (size_t)buf.stride + (size_t)px * 3u;
    const u8 r = pixels[offset + 0];
    const u8 g = pixels[offset + 1];
    const u8 b = pixels[offset + 2];

    ASSERT_TRUE_MSG(r > 100, "expected a strong red channel inside the rendered rectangle");
    ASSERT_TRUE_MSG(r > g && r > b, "expected red to dominate green and blue");
}

// The debug-only render-IR inspector: snapshot serialize, enumerate, and hit test by id and by name. The case is
// behind the same define as the inspector, so a build without it drops the case and still links.

#ifdef FLOWI_UI_INSPECTOR

UTEST(render, inspector_seam) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    // With two elements, enumerate and hit test have to choose between candidates. The second element's name
    // contains a quote and a backslash to cover the snapshot's JSON escaping. The fills make each element emit a
    // render command carrying its id, name and bounds.
    const float probe_w = 80.0f;
    const float probe_h = 40.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(ID("inspector_probe"), .sizing = { sizing_fixed(probe_w), sizing_fixed(probe_h) },
                          .color = ui_color(40, 200, 60, 255)) {}
        ui_layout_area_id(ID("a\"b\\c"), .sizing = { sizing_fixed(30), sizing_fixed(20) },
                          .color = ui_color(200, 40, 60, 255)) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    char snap[4096];
    const int32_t needed = fl_inspector_snapshot(snap, (int32_t)sizeof(snap));
    ASSERT_TRUE_MSG(needed > 0, "snapshot of a non-empty frame reports a non-zero length");
    ASSERT_TRUE_MSG(needed < (int32_t)sizeof(snap), "4KB snapshot buffer was large enough (not truncated)");
    ASSERT_EQ_MSG((int32_t)strlen(snap), needed, "snapshot NUL-terminates at exactly the reported length");
    ASSERT_TRUE_MSG(strstr(snap, "inspector_probe") != nullptr, "snapshot text mentions the known element");
    // The name a"b\c appears JSON-escaped as a\"b\\c. The C literal below is that byte sequence.
    ASSERT_TRUE_MSG(strstr(snap, "a\\\"b\\\\c") != nullptr, "snapshot escapes quotes and backslashes in names");

    // Truncation follows the snprintf contract.
    ASSERT_EQ_MSG(fl_inspector_snapshot(nullptr, 0), needed, "a zero-size buffer still reports the full length");
    char small[10];
    const int32_t small_needed = fl_inspector_snapshot(small, (int32_t)sizeof(small));
    ASSERT_EQ_MSG(small_needed, needed, "a too-small buffer still reports the full length to retry with");
    ASSERT_EQ_MSG(small[sizeof(small) - 1], '\0', "a truncated snapshot is still NUL-terminated");
    ASSERT_EQ_MSG((int32_t)strlen(small), (int32_t)sizeof(small) - 1, "a truncated snapshot fills up to the NUL");

    const int32_t n = fl_inspector_element_count();
    ASSERT_TRUE_MSG(n > 0, "the frame enumerates at least one element");
    FlInspectorElement probe = { 0 };
    bool found = false;
    for (int32_t i = 0; i < n; ++i) {
        FlInspectorElement e;
        ASSERT_TRUE_MSG(fl_inspector_element_get(i, &e), "enumerate returns each in-range element");
        if (e.name.length == (u64)(sizeof("inspector_probe") - 1)
            && memcmp(e.name.data, "inspector_probe", sizeof("inspector_probe") - 1) == 0) {
            probe = e;
            found = true;
            break;
        }
    }
    ASSERT_TRUE_MSG(found, "the known element is present in the enumeration");
    ASSERT_TRUE_MSG(probe.width == probe_w && probe.height == probe_h,
                    "the enumerated element carries the fixed size it was laid out with");
    FlInspectorElement oob;
    ASSERT_FALSE_MSG(fl_inspector_element_get(n, &oob), "an out-of-range index is rejected");

    FlInspectorHit by_id;
    ASSERT_TRUE_MSG(fl_inspector_hit_test_id(probe.id, &by_id), "hit-test by id finds the element");
    ASSERT_TRUE_MSG(by_id.x > probe.x && by_id.x < probe.x + probe.width,
                    "hit-test center x is inside the element's bounds");
    ASSERT_TRUE_MSG(by_id.y > probe.y && by_id.y < probe.y + probe.height,
                    "hit-test center y is inside the element's bounds");

    FlInspectorHit by_name;
    ASSERT_TRUE_MSG(
        fl_inspector_hit_test_name(string_from_cstr_len("inspector_probe", sizeof("inspector_probe") - 1), &by_name),
        "hit-test by name finds the element");
    ASSERT_EQ_MSG(by_name.id, by_id.id, "by-name and by-id hit-tests resolve to the same element");
    ASSERT_TRUE_MSG(by_name.x == by_id.x && by_name.y == by_id.y, "by-name and by-id centers agree");

    FlInspectorHit miss;
    ASSERT_FALSE_MSG(
        fl_inspector_hit_test_name(string_from_cstr_len("no_such_element", sizeof("no_such_element") - 1), &miss),
        "hit-test by an absent name reports no hit");

    // Clay never assigns id 0, because it reserves the zero hash as null.
    FlInspectorHit miss_id;
    ASSERT_FALSE_MSG(fl_inspector_hit_test_id(0u, &miss_id), "hit-test by an absent id reports no hit");
}

UTEST(render, inspector_capture_owns_strings) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    // The ids are built by hand and not with ID(). That macro marks its name as a static literal, and a static
    // string is not copied. Only a runtime string goes through the copy.
    char text_storage[] = CANARY_TEXT;
    char name_storage[] = CANARY_NAME;
    const u64 text_length = sizeof(CANARY_TEXT) - 1;
    const u64 name_length = sizeof(CANARY_NAME) - 1;
    const FlLayoutId dynamic_id = { .name = { .data = name_storage, .length = name_length } };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(dynamic_id, .sizing = { sizing_fixed(120), sizing_fixed(40) },
                          .color = ui_color(40, 200, 60, 255)) {
            ui_text(string_from_cstr_len(text_storage, text_length));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Stands in for the frame arena being rewound and reused after the capture.
    memory_set(text_storage, sizeof(text_storage), 'X', text_length);
    memory_set(name_storage, sizeof(name_storage), 'X', name_length);

    char snap[4096];
    const int32_t needed = fl_inspector_snapshot(snap, (int32_t)sizeof(snap));
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snap), "snapshot fits the buffer");
    ASSERT_TRUE_MSG(strstr(snap, CANARY_TEXT) != nullptr,
                    "captured text survives the caller's buffer being overwritten");
    ASSERT_TRUE_MSG(strstr(snap, CANARY_NAME) != nullptr,
                    "captured element name survives the caller's buffer being overwritten");
    ASSERT_TRUE_MSG(strstr(snap, "XXXX") == nullptr, "no scribbled bytes leak into the snapshot");

    // Every reader over the capture needs the same ownership, not only the JSON one.
    bool found = false;
    const int32_t n = fl_inspector_element_count();
    for (int32_t i = 0; i < n; ++i) {
        FlInspectorElement e;
        ASSERT_TRUE_MSG(fl_inspector_element_get(i, &e), "enumerate returns each in-range element");
        if (e.name.length == name_length && memcmp(e.name.data, CANARY_NAME, name_length) == 0) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE_MSG(found, "the enumerated name is the captured copy, not the overwritten original");

    FlInspectorHit hit;
    ASSERT_TRUE_MSG(fl_inspector_hit_test_name(string_from_cstr_len(CANARY_NAME, name_length), &hit),
                    "hit-test by name matches against the captured copy");
}

#endif // FLOWI_UI_INSPECTOR

// fl_icon_load and fl_icon_get over the stub VFS driver. image_load_async returns a request-id handle
// synchronously, so the handle is valid even though the stub serves no svg.

UTEST(render, icon_registry) {
    FlUiCtx* ctx = render_fixture_get(); // creates the VFS and image system and makes g_ui_ctx current for fl_icon_*
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Before any load the registry is the zeroed UiShared.icons[], which reads back as FL_IMAGE_HANDLE_INVALID.
    ASSERT_EQ_MSG(fl_icon_get(FlIcon_Checkmark), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "icon must be FL_IMAGE_HANDLE_INVALID before fl_icon_load");

    vfs_test_register_driver(&s_stub_vfs_plugin); // a mount source for fl_icon_load
    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    ASSERT_TRUE(mount.mount != nullptr);
    vfs_wait_all();

    // With a null mount nothing loads.
    fl_icon_load(nullptr);
    ASSERT_EQ_MSG(fl_icon_get(FlIcon_Checkmark), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "fl_icon_load(nullptr) must not load anything");

    fl_icon_load(mount.mount);
    // Fetching all three slots catches a wrong path or index in ICON_TABLE at any of them.
    const FlImageHandle checkmark = fl_icon_get(FlIcon_Checkmark);
    const FlImageHandle chevron = fl_icon_get(FlIcon_ChevronDown);
    const FlImageHandle warning = fl_icon_get(FlIcon_Warning);
    ASSERT_NE_MSG(checkmark, (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "fl_icon_get(Checkmark) must return a loaded handle");
    ASSERT_NE_MSG(chevron, (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "fl_icon_get(ChevronDown) must return a loaded handle");
    ASSERT_NE_MSG(warning, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "fl_icon_get(Warning) must return a loaded handle");
    ASSERT_EQ_MSG(fl_icon_get(FlIcon_Count), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "out-of-range icon must return FL_IMAGE_HANDLE_INVALID");
    ASSERT_EQ_MSG(fl_icon_get((FlIcon)-1), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "negative icon index must return FL_IMAGE_HANDLE_INVALID");

    fl_vfs_mount_close(mount.mount);
}

// fl_modal_begin_frame, begin, end and close. Focus is saved when the scope opens and restored, with all
// popup-scope state cleared, when it closes.

UTEST(render, modal_begin_frame_keeps_armed_scope) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 held_scope = 0x68656C64u;  // 'held'
    const u32 modal_scope = 0x6D6F6461u; // 'moda'
    ctx->popup_focus_scope_id = held_scope;
    ctx->saved_focused_id = 0x1111u;
    ctx->focused_id = 0x2222u;
    ctx->popup_needs_initial_focus = false;

    fl_modal_begin_frame(modal_scope);

    ASSERT_EQ_MSG(ctx->popup_focus_scope_id, held_scope, "an already-armed focus scope must stay armed");
    ASSERT_EQ_MSG(ctx->saved_focused_id, 0x1111u, "the saved focus must not be overwritten");
    ASSERT_FALSE_MSG(ctx->popup_needs_initial_focus, "no initial-focus request when the scope was not claimed");

    ctx->modal_scope_id = 0;
    ctx->popup_focus_scope_id = 0;
    ctx->saved_focused_id = 0;
}

UTEST(render, modal_scope) {
    FlUiCtx* ctx = render_fixture_get(); // also makes the fixture context current (g_ui_ctx), which these fl_* read
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 scope_id = 0x6D6F6461u; // 'moda'
    const u32 prior_focus = 0xABCD1234u;

    // A non-zero popup_focus_scope_id would make fl_modal_begin_frame skip the save.
    ctx->popup_focus_scope_id = 0;
    ctx->saved_focused_id = 0;

    const int layout_w = 256, layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    ui_update_begin(ctx);

    // A widget owns focus before the modal opens, and the scope is armed as a dialog does at the top of a frame.
    ctx->focused_id = prior_focus;
    fl_modal_begin_frame(scope_id);
    ASSERT_EQ_MSG(ctx->saved_focused_id, prior_focus, "fl_modal_begin_frame must save the pre-modal focus on open");
    ASSERT_EQ_MSG(ctx->popup_focus_scope_id, scope_id, "fl_modal_begin_frame must arm the modal focus scope");

    fl_modal_begin(scope_id);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(40, 40, 48, 255)) {}
    fl_modal_end();

    ui_update_end(ctx, 1.0f / 60.0f);

    fl_modal_close();
    ASSERT_EQ_MSG(ctx->focused_id, prior_focus, "fl_modal_close must restore the pre-modal focus");
    ASSERT_EQ_MSG(ctx->popup_focus_scope_id, 0u, "fl_modal_close must clear the modal focus scope");
    ASSERT_EQ_MSG(ctx->saved_focused_id, 0u, "fl_modal_close must clear the saved focus");
}

// fl_layout_area, fl_layout_area_id and FL_ID nested two or more deep. The open and close pairs have to balance.
// Clay's open-element id is read before the macros, inside the innermost body and after the macros unwind, so a
// leaked or duplicated fl_layout_end leaves a different id.

UTEST(render, layout_area_macros_nest) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const FlColor outer_color = ui_color(40, 40, 200, 255); // blue backdrop
    const FlColor inner_color = ui_color(40, 200, 60, 255); // green innermost child, anchored top-left

    ui_update_begin(ctx);

    // Clay_GetOpenElementId() reads the innermost open element. ui_update_begin has the root open, so this is the
    // baseline.
    const uint32_t base_open = Clay_GetOpenElementId();

    uint32_t inner_open = base_open;
    fl_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = outer_color) {
        fl_layout_area_id(FL_ID("layout_area_inner"), .sizing = { sizing_fixed(120), sizing_fixed(120) },
                          .color = inner_color) {
            inner_open = Clay_GetOpenElementId();
        }
    }

    ASSERT_NE_MSG(base_open, inner_open, "nested fl_layout_area macros must open their containers");
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(),
                  "fl_layout_area macros must close exactly as many containers as they open");

    ui_update_end(ctx, 1.0f / 60.0f);

    // The innermost 120x120 green child is at the top-left. Its color there shows that the nesting opened.
    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    const int px = 16, py = 16;
    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)py * (size_t)buf.stride + (size_t)px * 3u;
    ASSERT_TRUE_MSG(pixels[offset + 1] > 100, "expected a strong green channel inside the nested child");
    ASSERT_TRUE_MSG(pixels[offset + 1] > pixels[offset + 0] && pixels[offset + 1] > pixels[offset + 2],
                    "expected green (the innermost child) to dominate");
}

// A tracked single-word label is laid out as wide as its glyphs are painted. The element width equals the pen
// span, from the first glyph's origin to the end of the last glyph's advance, and zero spacing leaves the plain
// sum of advances.
UTEST(render, tracked_single_word_element_width_matches_the_painted_extent) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    const u16 size = 15;
    FlFontId font = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");

    const u16 spacings[] = { 0, 5 };
    float element_width[2] = { 0 };
    float painted_extent[2] = { 0 };

    for (u32 i = 0; i < 2; i++) {
        ui_update_begin(ctx);
        fl_layout_start(ID("tracked_word_root"));
        fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
        ui_text_styled(FL_S("AoA"), .font_id = font, .font_size = size, .letter_spacing = spacings[i],
                       .color = ui_color(255, 255, 255, 255));
        fl_layout_end();
        ui_update_end(ctx, 1.0f / 60.0f);

        ASSERT_TRUE_MSG(s_render.text_run_found, "the styled text must reach the frame as a text run");
        ASSERT_EQ_MSG((int)s_render.text_run_glyph_count, 3, "one glyph per codepoint");

        UiRenderCommand text;
        ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Text, &text, 1), 1, "the text element is in the snapshot");
        element_width[i] = text.width;

        const float last_advance
            = (float)laid_out_advance_q4(s_render.font_cache, font, font_cache_whole_px_to_q4(size), 'A') / 64.0f;
        painted_extent[i] = s_render.text_run_glyphs[2].x - s_render.text_run_glyphs[0].x + last_advance;
    }

    ASSERT_NEAR_MSG(element_width[0], painted_extent[0], 1.0f, "an untracked label fits its advance sum");
    ASSERT_NEAR_MSG(element_width[1], painted_extent[1], 1.0f, "a tracked label's box fits its painted ink");
    ASSERT_NEAR_MSG(element_width[1] - element_width[0], 10.0f, 1.0f, "two gaps of 5px separate the two widths");
}

// Auto-restore repairs focus when the focused widget disappears. It never gives focus to a widget when nothing
// was focused. The ring belongs to nobody until a Tab, a click or an explicit fl_set_focus moves it.

static void focus_idle_frame(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_button(FL_S("idle_first"));
        ui_button(FL_S("idle_second"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(render, idle_frame_leaves_the_keyboard_ring_unclaimed) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    render_use_default_font(ctx, 3);

    const u32 first_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("idle_first"), .id = 0 });

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    // The ring is animated, so one frame cannot show that it does not ramp. Run several.
    for (int frame = 0; frame < 3; frame++) {
        focus_idle_frame(ctx);
        ASSERT_EQ_MSG(fl_focused_id(), 0u, "an idle frame must leave the keyboard ring unclaimed");
    }
    ASSERT_FALSE_MSG(focus_ring_drawn(), "no widget strokes a focus border before the user aims at one");

    ui_update_begin(ctx);
    ctx->nav_next_pressed = true; // set after begin, like the directional flags the tab bar consumes
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_button(FL_S("idle_first"));
        ui_button(FL_S("idle_second"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_next_pressed = false;
    ASSERT_EQ_MSG(fl_focused_id(), first_id, "the first Tab must land on the first focusable widget");

    // A focused widget that leaves the frame is still repaired, to last_focused_id while that one exists.
    ctx->focused_id = 0xBADF0CU;
    focus_idle_frame(ctx);
    ASSERT_EQ_MSG(fl_focused_id(), first_id, "a vanished focused widget restores the last focused one");

    // When the last focused one is gone too, focus goes to the first focusable widget.
    ctx->focused_id = 0xBADF0CU;
    ctx->last_focused_id = 0xBADF0DU;
    focus_idle_frame(ctx);
    ASSERT_EQ_MSG(fl_focused_id(), first_id, "with no last focused widget the repair falls back to the first");

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
}
