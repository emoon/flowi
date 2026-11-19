// theme, widget skin and public widget API tests

#include "render/common.h"

// Theme colors are read through ui_get_theme_colors and not through the raw base pointer. With the override stack
// empty it returns the base theme the host injected, a context with no injected theme falls back to the built-in
// default theme, and every box color of a frame comes through it.

UTEST(render, theme_seam) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // ui_set_scale 1.0 so colors are unmodified, with no background image and a fixed clear color.
    render_set_layout_1080p(ctx);

    // A theme with a distinct value in every semantic slot.
    ui_set_theme(ctx, &k_known_theme);
    const FlThemeColors* resolved = ui_get_theme_colors(ctx);
    ASSERT_TRUE_MSG(resolved == &k_known_theme, "seam must resolve to the injected base theme");

    // Eight boxes, one per semantic group, each colored through ui_get_theme_colors.
    const FlColor slots[8] = {
        resolved->surface.base,      resolved->surface.elevated,     resolved->interactive.base,
        resolved->interactive.hover, resolved->border.default_color, resolved->text.primary,
        resolved->feedback.success,  resolved->feedback.error,
    };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = resolved->surface.base,
                   .direction = FlLayoutDirection_LeftToRight, .padding = fl_padding_md(), .child_gap = FlSpacing_LG) {
        for (int i = 0; i < 8; i++) {
            ui_layout_area(.sizing = { sizing_fixed(160), sizing_fixed(160) }, .color = slots[i]) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();
    dump_frame("theme_seam", &buf);

    // A context with no injected theme falls back to the built-in default theme.
    ui_set_theme(ctx, nullptr);
    const FlThemeColors* def = ui_get_theme_colors(ctx);
    ASSERT_TRUE_MSG(def != nullptr, "default resolution must never return null");
    ASSERT_TRUE_MSG(color_eq(def->surface.base, (FlColor)FL_THEME_DEFAULT_SURFACE_BASE),
                    "default fallback surface.base must match the default theme");
    ASSERT_TRUE_MSG(color_eq(def->text.primary, (FlColor)FL_THEME_DEFAULT_TEXT_PRIMARY),
                    "default fallback text.primary must match the default theme");
}

static void theme_scope_swatches(const FlColor* colors, int first, int last) {
    for (int i = first; i <= last; i++) {
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = colors[i]) {}
    }
}

UTEST(render, theme_scope) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);
    const FlThemeColors* base = &k_known_theme;
    ui_set_theme(ctx, base);

    const FlColor override_surface = { 60, 20, 20, 255 };     // override A: surface.base
    const FlColor override_interactive = { 20, 60, 20, 255 }; // override A: interactive.base
    const FlColor override_text = { 220, 220, 40, 255 };      // override B (nested): text.primary
    const FlColor per_object = { 10, 200, 200, 255 };         // explicit per-object color, never themed

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must start empty");

    FlColor captured[8];
    captured[0] = ui_get_theme_colors(ctx)->surface.base;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = base->surface.base,
                   .direction = FlLayoutDirection_LeftToRight, .padding = fl_padding_md(), .child_gap = FlSpacing_LG) {
        theme_scope_swatches(captured, 0, 0);

        ui_theme_push(ctx, (FlThemeOverride) {
                               .slots = FlThemeSlot_SurfaceBase | FlThemeSlot_InteractiveBase,
                               .colors = { .surface.base = override_surface, .interactive.base = override_interactive },
                           });
        const FlThemeColors* scope_a = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(scope_a->surface.base, override_surface), "A: surface.base must resolve to override");
        ASSERT_TRUE_MSG(color_eq(scope_a->interactive.base, override_interactive),
                        "A: interactive.base must resolve to override");
        ASSERT_TRUE_MSG(color_eq(scope_a->text.primary, base->text.primary),
                        "A: unflagged text.primary must inherit the base theme");
        captured[1] = scope_a->surface.base;
        captured[2] = scope_a->interactive.base;
        captured[3] = per_object;
        theme_scope_swatches(captured, 1, 3);

        ui_theme_push(ctx, (FlThemeOverride) {
                               .slots = FlThemeSlot_TextPrimary,
                               .colors = { .text.primary = override_text },
                           });
        const FlThemeColors* scope_b = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(scope_b->text.primary, override_text), "B: text.primary must resolve to override");
        ASSERT_TRUE_MSG(color_eq(scope_b->surface.base, override_surface),
                        "B: surface.base must inherit override A (nested stacking)");
        ASSERT_TRUE_MSG(color_eq(scope_b->interactive.base, override_interactive),
                        "B: interactive.base must inherit override A (nested stacking)");
        captured[4] = scope_b->text.primary;
        captured[5] = scope_b->surface.base;
        theme_scope_swatches(captured, 4, 5);

        ui_theme_pop(ctx);
        const FlThemeColors* scope_a2 = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(scope_a2->text.primary, base->text.primary),
                        "after popping B, text.primary must revert to the base theme");
        ASSERT_TRUE_MSG(color_eq(scope_a2->surface.base, override_surface),
                        "after popping B, surface.base must still be override A");
        captured[6] = scope_a2->text.primary;
        theme_scope_swatches(captured, 6, 6);

        ui_theme_pop(ctx);
        const FlThemeColors* outer = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(outer->surface.base, base->surface.base),
                        "after popping A, surface.base must revert to the base theme");
        ASSERT_TRUE_MSG(color_eq(outer->interactive.base, base->interactive.base),
                        "after popping A, interactive.base must revert to the base theme");
        captured[7] = outer->surface.base;
        theme_scope_swatches(captured, 7, 7);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must be balanced after equal push/pop");

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();
    dump_frame("theme_scope", &buf);
}

// A scoped override lasts one frame. If a caller pushes and does not pop, the override does not reach the next
// frame, because ui_update_begin resets the override depth to 0.

UTEST(render, theme_scope_frame_hygiene) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    static const FlThemeColors base = {
        .surface.base = { 18, 20, 24, 255 },
        .text.primary = { 240, 240, 240, 255 },
    };
    ui_set_theme(ctx, &base);

    // Push an override and do not pop it within the frame.
    ui_update_begin(ctx);
    ui_theme_push(ctx, (FlThemeOverride) {
                           .slots = FlThemeSlot_SurfaceBase,
                           .colors = { .surface.base = { 90, 10, 10, 255 } },
                       });
    ASSERT_EQ_MSG(ctx->theme_override_depth, 1, "push must raise the override depth");
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .color = ui_get_theme_colors(ctx)->surface.base) {}
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(ctx->theme_override_depth, 1, "forgotten pop leaves the override on the stack this frame");

    ui_update_begin(ctx);
    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "ui_update_begin must reset the override depth for the new frame");
    ASSERT_TRUE_MSG(color_eq(ui_get_theme_colors(ctx)->surface.base, base.surface.base),
                    "after frame reset, surface.base must resolve to the base theme");
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The theme override stack through the generated direct API: fl_theme_update injects the base palette,
// fl_theme_push adds a sparse override and fl_theme_pop removes it. The fl_theme_* forwarders use the file-local
// s_ctx, which ui_create sets in the fixture. base is static because fl_theme_update stores the pointer and the
// caller owns the storage.

UTEST(render, theme_stack_fl_api) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(g_ui_ctx == ctx, "fl_theme_* forward to s_ctx; fixture ctx must be the current ctx");

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },
        .text.primary = { 240, 240, 240, 255 },
    };
    const FlColor override_surface = { 60, 20, 20, 255 };

    fl_theme_update(&base);
    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must start empty");
    ASSERT_TRUE_MSG(color_eq(fl_theme_colors()->surface.base, base.surface.base),
                    "with no override pushed, surface.base must resolve to the injected base");

    fl_theme_push((FlThemeOverride) {
        .slots = FlThemeSlot_SurfaceBase,
        .colors = { .surface.base = override_surface },
    });
    ASSERT_EQ_MSG(ctx->theme_override_depth, 1, "push must raise the override depth");
    const FlThemeColors* scoped = fl_theme_colors();
    ASSERT_TRUE_MSG(color_eq(scoped->surface.base, override_surface),
                    "flagged surface.base must resolve to the override");
    ASSERT_TRUE_MSG(color_eq(scoped->surface.elevated, base.surface.elevated),
                    "unflagged surface.elevated must inherit the base theme");
    ASSERT_TRUE_MSG(color_eq(scoped->text.primary, base.text.primary),
                    "unflagged text.primary must inherit the base theme");

    fl_theme_pop();
    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "pop must restore the override depth");
    ASSERT_TRUE_MSG(color_eq(fl_theme_colors()->surface.base, base.surface.base),
                    "after pop, surface.base must revert to the injected base");
}

// The read getters: fl_theme_colors (the live palette), fl_focused_id (the focused widget id, without exposing
// FlUiCtx) and fl_list_clipper_create (the window of visible rows).

UTEST(render, public_read_getters) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(g_ui_ctx == ctx, "getters read the current ctx; fixture ctx must be current");

    static const FlThemeColors base = { .surface.base = { 21, 22, 23, 255 } };
    fl_theme_update(&base);
    const FlThemeColors* colors = fl_theme_colors();
    ASSERT_TRUE_MSG(colors != nullptr, "fl_theme_colors must return a live palette");
    ASSERT_TRUE_MSG(color_eq(colors->surface.base, base.surface.base),
                    "fl_theme_colors must resolve to the injected base palette");

    ASSERT_EQ_MSG(fl_focused_id(), 0u, "fl_focused_id must report 0 when nothing is focused");
    fl_set_focus(ID("getters_focus_target"));
    ASSERT_EQ_MSG(fl_focused_id(), ui_layout_id_to_clay_id(ID("getters_focus_target")),
                  "fl_focused_id must return the widget id that took focus");

    // 100 items of 20px scrolled 200px into a 100px viewport give rows [10, 16): first = 200/20 = 10 and
    // last = (200+100)/20 + 1 = 16.
    FlListClipper clip = fl_list_clipper_create(100, 20.0f, 200.0f, 100.0f);
    ASSERT_EQ_MSG(clip.display_start, 10, "clipper display_start must be 10 for the known viewport");
    ASSERT_EQ_MSG(clip.display_end, 16, "clipper display_end must be 16 for the known viewport");
    ASSERT_TRUE_MSG(clip.items_height == 2000.0f, "clipper items_height must be total_items * item_height");

    FlListClipper partial = fl_list_clipper_create(100, 20.0f, 30.0f, 15.0f);
    ASSERT_EQ_MSG(partial.display_start, 1, "a viewport straddling two rows must start at the first of them");
    ASSERT_EQ_MSG(partial.display_end, 3, "a viewport straddling two rows must include the second");

    FlListClipper past_end = fl_list_clipper_create(100, 20.0f, 5000.0f, 100.0f);
    ASSERT_EQ_MSG(past_end.display_start, 100, "scrolled past the end, the window must be empty at the end");
    ASSERT_EQ_MSG(past_end.display_end, 100, "scrolled past the end, the window must be empty at the end");

    // A zero item height (a row not yet measured on the first frame) would divide to inf, and converting that to
    // int is undefined behaviour. The window comes back empty.
    FlListClipper degenerate = fl_list_clipper_create(100, 0.0f, 200.0f, 100.0f);
    ASSERT_EQ_MSG(degenerate.display_start, 0, "zero item height must yield an empty window");
    ASSERT_EQ_MSG(degenerate.display_end, 0, "zero item height must yield an empty window");
}

// The public nav and action queries: fl_action_pressed reports the action that a bound key press raised this
// frame, and fl_set_focus and fl_focused_id round-trip a layout id.

UTEST(render, public_nav_query) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(g_ui_ctx == ctx, "fl_* nav queries read the current ctx; fixture ctx must be current");

    ctx->input_state.keyboard.keys_pressed[FlKey_Enter] = true;
    ui_actions_update(ctx);
    ASSERT_TRUE_MSG(fl_action_pressed(FlAction_Confirm), "fl_action_pressed must report a pressed action");
    ASSERT_FALSE_MSG(fl_action_pressed(FlAction_Cancel), "fl_action_pressed must report an unpressed action false");
    ctx->input_state.keyboard.keys_pressed[FlKey_Enter] = false;
    ui_actions_update(ctx);
    ASSERT_FALSE_MSG(fl_action_pressed(FlAction_Confirm), "fl_action_pressed must clear once the key is released");

    fl_set_focus(ID("nav_query_focus_target"));
    ASSERT_EQ_MSG(fl_focused_id(), ui_layout_id_to_clay_id(ID("nav_query_focus_target")),
                  "fl_set_focus + fl_focused_id must round-trip the resolved widget id");
}

// Every widget in the public fl_* API is called once inside one real frame, through its generated primary name
// and convenience macro. The frame emits render commands, which shows the widgets laid out. No interaction is
// injected, so every returned Signal is idle (.clicked false).

UTEST(render, public_widget_api_smoke) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(g_ui_ctx == ctx, "fl_* widgets forward to the current ctx; fixture ctx must be current");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    bool checked = false, switched = true, pilled = false, chipped = true, dropdown_open = false;
    int radio_sel = 0, dropdown_sel = 1, tab_sel = 0;
    float scroll = 0.25f;
    FlString radio_opts[] = { FL_S("One"), FL_S("Two"), FL_S("Three") };
    FlString dropdown_opts[] = { FL_S("Low"), FL_S("High") };
    FlString tabs[] = { FL_S("General"), FL_S("Audio") };

    // A small valid RGBA buffer, so the dynamic-image element refers to real memory.
    static u8 dyn_pixels[4 * 4 * 4] = { 0 };
    FlDynamicImage dyn = {
        .user_buffer = dyn_pixels,
        .width = 4,
        .height = 4,
        .user_stride = 4 * 4,
        .is_alpha_only = false,
    };

    bool clicked[8];
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        clicked[0] = fl_checkbox(FL_S("Check"), &checked).clicked;
        clicked[1] = fl_switch(FL_S("Switch"), &switched).clicked;
        clicked[2] = fl_radio_group(radio_opts, 3, &radio_sel).clicked;
        clicked[3] = fl_pill(FL_S("Pill"), &pilled).clicked;
        clicked[4] = fl_chip(FL_S("Chip"), &chipped).clicked;
        fl_progress_bar(0.5f);
        clicked[5] = fl_scrollbar(&scroll).clicked;
        fl_dropdown(FL_S("Quality"), &dropdown_open, dropdown_opts, 2, &dropdown_sel);
        clicked[6] = fl_tab_bar(tabs, 2, &tab_sel);
        clicked[7] = fl_link(FL_S("Click here")).clicked;
        fl_image(0, (FlImageOpts) { .color = ui_color(255, 255, 255,
                                                      255) }); // the handle is invalid, so ui_image_impl does nothing
        fl_dynamic_image(&dyn, (FlImageOpts) { .color = ui_color(255, 255, 255, 255) });
        fl_spacer(8.0f);
        fl_spacer_h(8.0f);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    const char* const no_input_msgs[8] = {
        "checkbox must not report a click with no input",
        "switch must not report a click with no input",
        "radio group must not report a selection change with no input",
        "pill must not report a click with no input",
        "chip must not report a click with no input",
        "scrollbar must not report a click with no input",
        "tab bar must not report a change with no input",
        "link must not report a click with no input",
    };
    for (int i = 0; i < 8; i++) {
        ASSERT_FALSE_MSG(clicked[i], no_input_msgs[i]);
    }

    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "public widget frame must capture render commands");

    ASSERT_EQ_MSG(radio_sel, 0, "radio selection unchanged with no input");
    ASSERT_EQ_MSG(tab_sel, 0, "tab selection unchanged with no input");
    ASSERT_EQ_MSG(dropdown_sel, 1, "dropdown selection unchanged with no input");
    ASSERT_FALSE_MSG(dropdown_open, "dropdown stays closed with no input");
    // The scrollbar recomputes *value from the thumb geometry every frame, so it is not asserted unchanged.
}

// Interactive-state color derivation. The case calls fl_resolve_widget_colors for a synthetic ItemState at stepped
// hot_t, active_t and focused_t factors, which covers the OKLCH lighten and darken for hover and press and the
// OKLab focus-border lerp. It checks the endpoints the theme fixes and the direction each channel moves.

enum { STATE_DERIVATION_STEPS = 5 };

typedef enum StateChannel {
    StateChannel_Hover,
    StateChannel_Press,
    StateChannel_Focus,
} StateChannel;

// The background (hover, press) or border (focus) the variant resolves to with one animation factor set to t.
static FlColor state_derivation_resolve(FlWidgetVariant variant, StateChannel channel, float t,
                                        const FlThemeColors* theme) {
    ItemState item = { 0 };
    if (channel == StateChannel_Hover) {
        item.state.hot_t = t;
    } else if (channel == StateChannel_Press) {
        item.state.active_t = t;
    } else {
        item.state.focused_t = t;
    }
    const FlWidgetColors colors = fl_resolve_widget_colors(variant, (FlColor) { 0 }, (FlColor) { 0 }, &item, theme,
                                                           /*animate_text=*/true);
    return channel == StateChannel_Focus ? colors.border : colors.bg;
}

static int channel_sum(FlColor c) {
    return (int)c.r + (int)c.g + (int)c.b;
}

typedef struct StateRamps {
    FlColor hover[STATE_DERIVATION_STEPS];
    FlColor press[STATE_DERIVATION_STEPS];
    FlColor focus[STATE_DERIVATION_STEPS];
} StateRamps;

static void state_ramps_resolve(StateRamps* ramps, FlWidgetVariant variant, const float* steps,
                                const FlThemeColors* theme) {
    for (int s = 0; s < STATE_DERIVATION_STEPS; s++) {
        ramps->hover[s] = state_derivation_resolve(variant, StateChannel_Hover, steps[s], theme);
        ramps->press[s] = state_derivation_resolve(variant, StateChannel_Press, steps[s], theme);
        ramps->focus[s] = state_derivation_resolve(variant, StateChannel_Focus, steps[s], theme);
    }
}

static void check_state_ramp_endpoints(int* utest_result, const StateRamps* ramps, FlColor rest_bg,
                                       FlColor focus_color) {
    ASSERT_TRUE_MSG(color_eq(ramps->hover[0], rest_bg), "no hover resolves to the resting background");
    ASSERT_TRUE_MSG(color_eq(ramps->press[0], rest_bg), "no press resolves to the resting background");

    const FlColor full = ramps->focus[STATE_DERIVATION_STEPS - 1];
    const int got[4] = { full.r, full.g, full.b, full.a };
    const int want[4] = { focus_color.r, focus_color.g, focus_color.b, 255 };
    const char* const msgs[4] = { "full focus border red", "full focus border green", "full focus border blue",
                                  "full focus border is opaque" };
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ_MSG(got[i], want[i], msgs[i]);
    }
}

static void check_state_ramp_directions(int* utest_result, const StateRamps* ramps) {
    for (int s = 1; s < STATE_DERIVATION_STEPS; s++) {
        ASSERT_GT_MSG(channel_sum(ramps->hover[s]), channel_sum(ramps->hover[s - 1]), "hover lightens at every step");
        ASSERT_LE_MSG(channel_sum(ramps->press[s]), channel_sum(ramps->press[s - 1]), "press never lightens");
        ASSERT_GT_MSG(channel_sum(ramps->focus[s]) + ramps->focus[s].a,
                      channel_sum(ramps->focus[s - 1]) + ramps->focus[s - 1].a,
                      "the focus border moves toward the focus color at every step");
    }
    ASSERT_LT_MSG(channel_sum(ramps->press[STATE_DERIVATION_STEPS - 1]), channel_sum(ramps->press[0]),
                  "full press is darker than rest");
}

UTEST(render, state_derivation) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A fixed theme, so every input to the derivation is known.
    const FlThemeColors* known = &k_known_theme;
    ui_set_theme(ctx, known);
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    ASSERT_TRUE_MSG(theme == known, "seam must resolve to the injected theme");

    const float steps[STATE_DERIVATION_STEPS] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const FlWidgetVariant variants[4]
        = { FlWidgetVariant_Solid, FlWidgetVariant_Outline, FlWidgetVariant_Ghost, FlWidgetVariant_Soft };
    const FlColor rest_bg[4] = {
        known->interactive.base,
        known->surface.base,
        known->surface.base,
        { known->interactive.base.r, known->interactive.base.g, known->interactive.base.b, 38 },
    };

    for (int v = 0; v < 4; v++) {
        StateRamps ramps;
        state_ramps_resolve(&ramps, variants[v], steps, theme);
        check_state_ramp_endpoints(utest_result, &ramps, rest_bg[v], known->interactive.focus);
        RETURN_IF_FAILED();
        check_state_ramp_directions(utest_result, &ramps);
        RETURN_IF_FAILED();
    }
}

// Perceptual variant derivation from an arbitrary base color, plus a label color chosen by luminance. Lighten
// raises the brightness and darken lowers it, desaturate narrows the channel spread, and the readable foreground
// flips with the background.

static int variant_derivation_spread(FlColor c) {
    return (int)max(max(c.r, c.g), c.b) - (int)min(min(c.r, c.g), c.b);
}

UTEST(render, variant_derivation) {
    const FlColor label_light = FL_THEME_DEFAULT_TEXT_PRIMARY;
    const FlColor label_dark = { 16, 16, 18, 255 };

    // Derivation amounts an app might choose. The framework does not prescribe a set.
    const float lighten_amt = 0.12f, darken_amt = 0.12f, desat_amt = 0.5f;

    const struct {
        FlColor base;
        bool saturated;
        bool dark_label; // the label the base and its lighter and darker siblings take
    } rows[] = {
        { { 230, 230, 236, 255 }, false, true }, { { 20, 20, 24, 255 }, false, false },
        { { 236, 202, 42, 255 }, true, true },   { { 40, 84, 224, 255 }, true, false },
        { { 214, 40, 48, 255 }, true, false },
    };

    for (size_t r = 0; r < sizeof_array(rows); r++) {
        const FlColor base = rows[r].base;
        const FlColor lighter = color_lighten_perceptual(base, lighten_amt);
        const FlColor darker = color_darken_perceptual(base, darken_amt);
        const FlColor muted = color_desaturate_perceptual(base, desat_amt);

        ASSERT_GE_MSG(channel_sum(lighter), channel_sum(base), "lighten does not darken");
        ASSERT_LE_MSG(channel_sum(darker), channel_sum(base), "darken does not lighten");
        ASSERT_GT_MSG(channel_sum(lighter), channel_sum(darker), "the lighter sibling is brighter than the darker one");
        if (rows[r].saturated) {
            ASSERT_LT_MSG(variant_derivation_spread(muted), variant_derivation_spread(base),
                          "desaturate narrows the channel spread");
        }
    }

    const FlColor pale = rows[0].base;
    const FlColor near_black = rows[1].base;
    const FlColor yellow = rows[2].base;
    ASSERT_TRUE_MSG(color_eq(color_readable_foreground(pale, label_light, label_dark), label_dark),
                    "a pale background takes the dark label");
    ASSERT_TRUE_MSG(color_eq(color_readable_foreground(yellow, label_light, label_dark), label_dark),
                    "a saturated yellow background takes the dark label");
    ASSERT_TRUE_MSG(color_eq(color_readable_foreground(near_black, label_light, label_dark), label_light),
                    "a near-black background takes the light label");
}

// Per-widget state-slot overrides (FlWidgetConfig::theme_override). A widget config carries a sparse
// FlThemeOverride applied over the resolved theme for that one widget. It composes with any active scoped
// subtree override: the widget-level one wins for the slots it flags and the rest are inherited. The case uses
// real Outline buttons, whose resting background is surface.base and border interactive.base, and reads them
// back from the debug render-command snapshot.

// Collects the opaque background (rectangle) or border colors painted in the last frame, in command order. Each
// Outline button emits exactly one fully opaque rectangle (its background) and one opaque border, in declaration
// order. The container draws neither, and the link drawn at the end paints only nearly transparent helper rects,
// which the alpha == 255 filter excludes. So the k-th opaque rectangle and the k-th opaque border belong to the
// k-th button.
static bool slot_override_opaque_color(int index, UiRenderCommandType type, UiColor4* out) {
    UiRenderCommand cmd;
    if (!ui_get_render_command(index, &cmd) || cmd.type != type) {
        return false;
    }
    *out = (type == UiRenderCmd_Border) ? cmd.border_color : cmd.bg_color;
    return out->a >= 255.0f; // the link's nearly transparent container and underline helpers are skipped
}

static int slot_override_collect(UiRenderCommandType type, UiColor4* out, int max) {
    int count = 0;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n && count < max; i++) {
        count += slot_override_opaque_color(i, type, &out[count]) ? 1 : 0;
    }
    return count;
}

// Finds a text command whose color matches want. The link's text color is its per-widget interactive.base
// override.
static bool slot_override_is_text_color(int index, FlColor want) {
    UiRenderCommand cmd;
    if (!ui_get_render_command(index, &cmd)) {
        return false;
    }
    return cmd.type == UiRenderCmd_Text && slot_override_color_eq(cmd.text_color, want);
}

static bool slot_override_has_text_color(FlColor want) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        if (slot_override_is_text_color(i, want)) {
            return true;
        }
    }
    return false;
}

// Focus ring (FlThemeSlot_InteractiveFocus). It uses a synthetic focused ItemState because the render path's focus
// animation cannot reach focused_t=1 in a single frame. A Solid variant has a transparent base border, so at
// focused_t=1 the focus border is exactly interactive.focus, an alpha ramp with no OKLab rounding.
static void check_focus_ring_override(int* utest_result, const FlThemeColors* base) {
    const FlColor focus_ov_color = { 12, 240, 200, 255 };
    const FlThemeOverride ov_focus
        = { .slots = FlThemeSlot_InteractiveFocus, .colors = { .interactive.focus = focus_ov_color } };
    const FlThemeColors focus_merged = fl_theme_merge_override(base, &ov_focus);
    ASSERT_TRUE_MSG(color_eq(focus_merged.interactive.focus, focus_ov_color), "focus slot merges per-widget");
    ItemState focused = { 0 };
    focused.state.focused_t = 1.0f;
    const FlWidgetColors focus_with_ov = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 },
                                                                  (FlColor) { 0 }, &focused, &focus_merged, false);
    const FlWidgetColors focus_no_ov
        = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &focused, base, false);
    ASSERT_TRUE_MSG(color_eq(focus_with_ov.border, focus_ov_color),
                    "focus-ring override drives the derived focus ring");
    ASSERT_TRUE_MSG(color_eq(focus_no_ov.border, base->interactive.focus),
                    "without override the focus ring is the base focus");
    ASSERT_TRUE_MSG(!color_eq(focus_with_ov.border, focus_no_ov.border), "focus-ring override changes the focus ring");
}

UTEST(render, widget_slot_override) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);

    // The built-in 8x8 font, which needs no asset. Buttons paint text.
    render_use_default_font(ctx, 3);

    const FlThemeColors* base = &k_skin_theme;
    ui_set_theme(ctx, base);

    const FlColor S1 = { 60, 20, 20, 255 };  // subtree surface.base
    const FlColor I1 = { 20, 60, 20, 255 };  // subtree interactive.base
    const FlColor I2 = { 40, 40, 200, 255 }; // widget override: interactive.base (border)
    const FlColor S2 = { 70, 50, 10, 255 };  // widget override: surface.base (background)

    // The per-widget overrides are named locals because a compound literal inside the ui_button() macro would
    // split on its internal commas. The preprocessor protects parentheses but not braces.
    const FlThemeOverride ov_interactive
        = { .slots = FlThemeSlot_InteractiveBase, .colors = { .interactive.base = I2 } };
    const FlThemeOverride ov_surface = { .slots = FlThemeSlot_SurfaceBase, .colors = { .surface.base = S2 } };

    // A link uses the nested cfg.widget.theme_override, where a button uses cfg.theme_override. At rest its text
    // is drawn in interactive.base, so a unique override color can be found among the text commands.
    const FlColor LINK_OV = { 123, 60, 200, 255 };
    const FlThemeOverride ov_link = { .slots = FlThemeSlot_InteractiveBase, .colors = { .interactive.base = LINK_OV } };

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must start empty");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom, .padding = fl_padding_md(), .child_gap = FlSpacing_MD) {
        ui_button(FL_S("plain"), .variant = FlWidgetVariant_Outline);

        ui_theme_push(ctx, (FlThemeOverride) {
                               .slots = FlThemeSlot_SurfaceBase | FlThemeSlot_InteractiveBase,
                               .colors = { .surface.base = S1, .interactive.base = I1 },
                           });
        ui_button(FL_S("sib"), .variant = FlWidgetVariant_Outline);
        ui_button(FL_S("wbase"), .variant = FlWidgetVariant_Outline, .theme_override = &ov_interactive);
        ui_button(FL_S("wsurf"), .variant = FlWidgetVariant_Outline, .theme_override = &ov_surface);
        ui_theme_pop(ctx);

        ui_button(FL_S("plain2"), .variant = FlWidgetVariant_Outline);

        ui_link(FL_S("lnk"), .widget.theme_override = &ov_link);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must be balanced after equal push/pop");

    // Five buttons, in declaration order: plain, sib, wbase, wsurf, plain2.
    UiColor4 bg[5], border[5];
    ASSERT_EQ_MSG(slot_override_collect(UiRenderCmd_Rectangle, bg, 5), 5, "expected one background per button");
    ASSERT_EQ_MSG(slot_override_collect(UiRenderCmd_Border, border, 5), 5, "expected one border per button");

    const struct {
        FlColor want_bg;
        FlColor want_border;
        const char* bg_msg;
        const char* border_msg;
    } buttons[5] = {
        { base->surface.base, base->interactive.base, "plain: bg resolves to base surface",
          "plain: border resolves to base interactive" },
        { S1, I1, "sib: bg resolves to the subtree override", "sib: border resolves to the subtree override" },
        { S1, I2, "wbase: bg inherits the subtree surface override",
          "wbase: border wins with the per-widget override" },
        { S2, I1, "wsurf: bg wins with the per-widget override",
          "wsurf: border inherits the subtree interactive override" },
        { base->surface.base, base->interactive.base, "plain2: bg reverts to base after pop",
          "plain2: border reverts to base after pop" },
    };
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE_MSG(slot_override_color_eq(bg[i], buttons[i].want_bg), buttons[i].bg_msg);
        ASSERT_TRUE_MSG(slot_override_color_eq(border[i], buttons[i].want_border), buttons[i].border_msg);
    }

    ASSERT_TRUE_MSG(slot_override_has_text_color(LINK_OV), "link: cfg.widget.theme_override retints the link text");

    check_focus_ring_override(utest_result, base);

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();
    dump_frame("widget_slot_override", &buf);
}

// A switch with a skin override rendered next to a default one.
//
// The asserts are computed from the geometry: at the mid-height of the skinned row the frame is back to the row
// background past x=26, where the skinned track ends, while the default row is still painted at the same x,
// since its track runs to 48.
UTEST(render, switch_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);

    const FlThemeColors* base = &k_skin_theme;
    ui_set_theme(ctx, base);

    // The built-in 8x8 font at scale 1, which needs no asset. Scale 1 keeps each label inside its own row, and
    // the skinned row is only 13px tall.
    render_use_default_font(ctx, 1);

    // Two rows at the top-left with no gap: the default 48x24 pill, then the compact 26x13 skin. Each switch's
    // own container is {grow, fixed(track_height)}, so row 0 spans y 0..24 and row 1 y 24..37, with both tracks
    // at x = 0.
    bool off_default = false;
    bool off_skin = false;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
        ui_switch(FL_S("default"), &off_default, .track_width = 0.0f);
        ui_switch(FL_S("skin"), &off_skin, .track_width = 26.0f, .track_height = 13.0f);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    // x = 36 is inside the default track (0..48) and well past the skinned one's right edge (26). The two rows'
    // mid-heights are y = 12 and y = 30. Each row is compared with bare background in the same row (x = 200, past
    // both tracks) and not with the clear color, because every switch lays its hit-test container across the
    // full row width at alpha 2, so an unpainted pixel has the container tint.
#define SWITCH_PX_EQ(ax, ay, bx, by) \
    (memcmp(readback_rgb_at(&buf, (ax), (ay)), readback_rgb_at(&buf, (bx), (by)), 3) == 0)
    ASSERT_FALSE_MSG(SWITCH_PX_EQ(36, 12, 200, 12), "the default track still covers x = 36");
    ASSERT_TRUE_MSG(SWITCH_PX_EQ(36, 30, 200, 30), "the skinned track stops short of x = 36");
    // Both tracks cover x = 10, so the check above shows a difference in width and not a missing skinned switch.
    ASSERT_FALSE_MSG(SWITCH_PX_EQ(10, 30, 200, 30), "the skinned track is painted at x = 10");
#undef SWITCH_PX_EQ

    dump_frame("switch_skin", &buf);
}

// A slider with a skin override rendered above a default one.
//
// The assert is computed from the geometry: a column through both tracks, clear of the thumbs and of both
// labels, crosses exactly two runs of the fill color, 8px for the default bar and then 3px for the skinned one.
// The sliders are at rest, so the fill is theme->text.primary unchanged (ui_apply_state_color is the identity at
// hot_t and active_t 0).
UTEST(render, slider_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);

    const FlThemeColors* base = &k_skin_theme;
    ui_set_theme(ctx, base);

    // The built-in 8x8 font at scale 1, which needs no asset.
    render_use_default_font(ctx, 1);

    // Two sliders at the top-left: the default 24px thumb over an 8px bar, then the compact 13px thumb over a 3px
    // bar, both at half value. The labels are also the widget ids. There are two frames because the segment
    // widths come from the track row's width in the previous frame, so only the second is asserted.
    float value_default = 0.5f;
    float value_skin = 0.5f;
    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
            ui_slider_float(FL_S("slider_skin_default"), &value_default, 0.0f, 1.0f, .thumb_size = 0.0f);
            ui_slider_float(FL_S("slider_skin_thin"), &value_skin, 0.0f, 1.0f, .track_height = 3.0f,
                            .thumb_size = 13.0f);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    // Walk x = 200 from top to bottom and measure each run of fill-colored pixels. The tracks grow to 1000px
    // wide, so 200 is inside both filled segments. It is clear of the thumbs (mid travel, around x = 490) and of
    // both labels (a few 8px glyphs at the left edge), which are drawn in the same color. The two run lengths
    // are therefore the two bar thicknesses, in layout order.
    const u8 fill_rgb[3] = { 240, 240, 240 };
    RunList runs = { 0 };
    column_runs(&buf, 200, fill_rgb, &runs);
    ASSERT_EQ_MSG(runs.count, 2, "the column crosses exactly the two sliders' filled bars");
    ASSERT_EQ_MSG(runs.values[0], 8, "the default bar is 8px thick");
    ASSERT_EQ_MSG(runs.values[1], 3, "the skinned bar is 3px thick");

    dump_frame("slider_skin", &buf);
}

// A tab bar skinned as a segmented control, rendered under a default one. The default bar uses the zeroed config
// the dock leaf header also passes.
//
// The check is a scanline through the top of the pill, above the labels and positioned from the snapshot. It crosses
// exactly one run of the accent fill (the selected tab), and the strip shows the pill's own surface color between its
// edge and that tab.
UTEST(render, tab_bar_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);

    const FlThemeColors* base = &k_skin_theme;
    ui_set_theme(ctx, base);

    // The built-in 8x8 font at scale 1, which needs no asset.
    render_use_default_font(ctx, 1);

    // Hover lightens the fills, which would move the scanline's colors off their theme values.
    render_mouse_away(ctx);

    // The label sets differ because a label is also its tab's id, and the fixture's ItemState map outlives a
    // case.
    FlString tabs_default[] = { FL_S("Mix"), FL_S("Fx"), FL_S("Out") };
    FlString tabs_skin[] = { FL_S("Osc"), FL_S("Env"), FL_S("Mod") };
    int sel_default = 0;
    int sel_skin = 0;
    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
            ui_tab_bar(tabs_default, 3, &sel_default);
            ui_tab_bar(tabs_skin, 3, &sel_skin, .item_padding_h = 18.0f, .item_padding_v = 6.0f, .item_radius = 6.0f,
                       .container_padding = 3.0f, .container_radius = 8.0f, .font_size = 10);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    // Rectangles in render order: the default bar's three tabs, then the skinned strip's pill and its tabs.
    UiRenderCommand rects[8];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 8), 7,
                  "three default tabs, then the pill and three more");
    const UiRenderCommand pill = rects[3];
    const UiRenderCommand first_tab = rects[4];

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    // Scan a row near the pill's top across its full width. The selected tab is the only accent fill on it.
    const u8 accent_rgb[3] = { 200, 30, 40 };
    const u8 pill_rgb[3] = { 36, 38, 44 };
    const int scan_y = (int)pill.y + 4;
    int accent_runs = 0;
    bool in_run = false;
    for (int x = (int)pill.x; x < (int)(pill.x + pill.width); x++) {
        const bool accent = memcmp(readback_rgb_at(&buf, x, scan_y), accent_rgb, 3) == 0;
        if (accent && !in_run) {
            accent_runs++;
        }
        in_run = accent;
    }
    ASSERT_EQ_MSG(accent_runs, 1, "one accent run across the strip: the selected tab");
    ASSERT_EQ_MSG(memcmp(readback_rgb_at(&buf, (int)first_tab.x - 2, scan_y), pill_rgb, 3), 0,
                  "the pill paints behind the tabs, showing through its own padding");

    dump_frame("tab_bar_skin", &buf);
}

// A dropdown trigger skinned as a pill, rendered under a default one.
//
// The check is computed from the snapshot's own rects. A column through both triggers, clear of their labels and
// chevrons, crosses exactly two runs of the surface fill, and the skinned run is shorter than the default one by
// exactly the height the config removed. Taking the difference cancels the border at both ends.
UTEST(render, dropdown_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);

    // The fill, border and label colors come straight from these theme slots.
    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // the Outline trigger's border
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 30, 34, 42, 255 }, // the trigger's fill, and the run the scan below counts
        .surface.elevated = { 36, 38, 44, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    // The built-in 8x8 font at scale 1, which needs no asset.
    render_use_default_font(ctx, 1);

    // Hover lightens the fill, which would move the scanned run off its theme color.
    render_mouse_away(ctx);

    // The labels differ because a label is also its trigger's id, and the fixture's ItemState map outlives a
    // case.
    FlString opts[2] = { FL_S("Sine"), FL_S("Saw") };
    bool open_default = false;
    bool open_skin = false;
    int sel_default = 0;
    int sel_skin = 0;
    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
            ui_dropdown(FL_S("Wave"), &open_default, opts, 2, &sel_default);
            ui_dropdown(FL_S("Shape"), &open_skin, opts, 2, &sel_skin, .padding_h = 12.0f, .padding_v = 7.0f,
                        .corner_radius = 8.0f, .min_width = 180.0f);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    // One rectangle per closed trigger, in layout order.
    UiRenderCommand rects[4];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 4), 2, "one rectangle per closed trigger");
    const float height_taken_off = rects[0].height - rects[1].height;
    ASSERT_GT_MSG((double)height_taken_off, 0.0, "the pill is the shorter of the two");

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    // Walk a column down through both triggers and measure each run of the fill color. The triggers grow to the
    // full window width, so a column three quarters of the way across is well past the labels at their left edge
    // and well before the chevrons at their right, with only fill between the top and bottom borders.
    const u8 fill_rgb[3] = { 30, 34, 42 };
    const int scan_x = (int)(rects[0].x + rects[0].width * 0.75f);
    RunList runs = { 0 };
    column_runs(&buf, scan_x, fill_rgb, &runs);
    ASSERT_EQ_MSG(runs.count, 2, "the column crosses exactly the two triggers' fills");
    ASSERT_EQ_MSG(runs.values[0] - runs.values[1], (int)height_taken_off,
                  "the painted pill falls short of the default trigger by exactly the height padding_v took off");

    dump_frame("dropdown_skin", &buf);
}

// The dock leaf header's tab strip, rendered through dock_layout.c. ui_tab_strip is shared between the public tab
// bar and docking, and docking passes a zeroed FlTabBarConfig, so no geometry field has any effect there.
UTEST(render, dock_tabs) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);

    const FlThemeColors* base = &k_skin_theme;
    ui_set_theme(ctx, base);
    render_use_default_font(ctx, 1);
    ctx->input_state.mouse.x = -1000.0f; // hover would move the fills off their theme colors
    ctx->input_state.mouse.y = -1000.0f;

    // One root leaf holding two panels, so the leaf renders a tab header.
    FlDockWorkspace* ws = dock_workspace_create();
    ASSERT_TRUE_MSG(ws != nullptr, "dock_workspace_create failed");
    dock_workspace_set_tree(ws, dock_tree_build(dock_leaf(100, 101)));

    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        dock_workspace_begin(ws);
        // Titles first, so both passes render the same labelled strip.
        dock_workspace_declare(ws, 100, FL_S("Console"));
        dock_workspace_declare(ws, 101, FL_S("Log"));
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            dock_workspace_emit(ws);
        }
        dock_workspace_end(ws);
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    // The leaf paints no background of its own, so the frame holds the header bar and the two tabs on it. There
    // is no pill, because container_padding and container_radius are zero. In declaration order the bar comes
    // first, then the selected tab, then the unselected one.
    UiRenderCommand rects[8];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 8), 3,
                  "the header bar and its two tabs, with no pill among them");
    ASSERT_NEAR_MSG((double)rects[0].width, (double)ctx->layout_width, 1e-4, "the header bar spans the tile");
    ASSERT_TRUE_MSG(tab_color_eq(rects[0].bg_color, base->surface.elevated), "and reads as a raised surface");
    ASSERT_NEAR_MSG((double)rects[1].height, (double)DOCK_TAB_HEIGHT, 1e-4,
                    "a dock tab is a title bar's height, not a button's");
    ASSERT_NEAR_MSG((double)rects[1].corner_radius.tl, (double)FlRadius_SM, 1e-4, "at the dock's tab radius");
    // Two panels share this bar, so neither tab stretches. The selected one is Soft, an accent tint that can
    // cover the full width of a one-panel header, which the accent fill of a plain tab bar could not.
    const FlColor soft = { base->interactive.base.r, base->interactive.base.g, base->interactive.base.b, 38 };
    ASSERT_TRUE_MSG(tab_color_eq(rects[1].bg_color, soft), "the selected dock tab tints with the accent");
    ASSERT_TRUE_MSG(tab_color_eq(rects[2].bg_color, base->surface.base), "an unselected dock tab fills from the theme");

    FlPixelReadback buf;
    read_frame(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    dump_frame("dock_tabs", &buf);

    dock_workspace_destroy(ws);
}

// The public fl_button takes a trailing FlWidgetConfig, so the designated-initializer macro can pass .variant, as
// well as id, color and focus. The case reads the emitted background fills. At rest, with no hover or press, the
// variant's base color is the fill, so Solid (interactive.base) and Outline (surface.base) differ.

UTEST(render, public_button_variant_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 256.0f, 256.0f);
    render_use_default_font(ctx, 3);

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // Solid bg
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 }, // Outline bg
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
    };
    ui_set_theme(ctx, &base);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom, .padding = fl_padding_md(), .child_gap = FlSpacing_MD) {
        fl_button(FL_S("solid"), .variant = FlWidgetVariant_Solid);
        fl_button(FL_S("outline"), .variant = FlWidgetVariant_Outline);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Two buttons in declaration order, solid then outline, with one opaque background fill each.
    UiColor4 bg[2];
    ASSERT_EQ_MSG(slot_override_collect(UiRenderCmd_Rectangle, bg, 2), 2, "expected one background per button");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[0], base.interactive.base),
                    "Solid variant via fl_button config resolves bg to interactive.base");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[1], base.surface.base),
                    "Outline variant via fl_button config resolves bg to surface.base");
}
