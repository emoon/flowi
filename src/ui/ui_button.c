// Button family widgets: button, button-with-image, progress bar, link.

#include "ui.h"
#include "font_cache.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_variant_helpers.h"
#include "ui_color_utils.h"
#include "ui_tokens.h"
#include "layout.h"
#include "image.h"
#include <core/core.h>
#include <core/math.h>
#include <external/clay/clay.h>

// Button-with-image dimensions (large menu items).
static const int UI_BUTTON_WITH_IMAGE_WIDTH = 400;
static const int UI_BUTTON_WITH_IMAGE_HEIGHT = 100;

static inline FlLayoutSizingAxis get_widget_height_sizing(FlWidgetSize size) {
    switch (size) {
        case FlWidgetSize_SM:
            return sizing_fixed(FlWidgetHeight_SM);
        case FlWidgetSize_LG:
            return sizing_fixed(FlWidgetHeight_LG);
        case FlWidgetSize_MD:
        case FlWidgetSize_Default:
        default:
            return sizing_fixed(FlWidgetHeight_MD);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlSignal ui_button_impl(FlString string, FlWidgetConfig cfg) {
    UiWidgetState w = ui_widget_prepare(string, cfg, STANDARD_WIDGET_FLAGS, cfg.variant, /*animate_text=*/false);
    FlSignal signal = w.signal;
    FlColor bg_color = w.colors.bg;
    FlColor text_color = w.colors.text;
    FlColor border_color = w.colors.border;
    FlLayoutId button_id = w.id;

    float radius = FlRadius_MD;
    FlLayoutSizingAxis height = get_widget_height_sizing(cfg.size);

    ui_layout_area_id(button_id, .sizing = { sizing_fit(), height }, .direction = LayoutDirection_TopToBottom,
                      .child_alignment = ui_align(Center, Center), .padding = fl_padding_hv(FlSpacing_MD, FlSpacing_SM),
                      .radius = corner_radius(radius), .color = bg_color,
                      .border = fl_border(border_color, FlBorderWidth_Medium)) {
        ui_text_styled_impl(string, (FlTextConfig) { .color = text_color, .valign = FlTextVAlign_Center });
    }

    return signal;
}

// clang-format off
FlSignal ui_button_with_image_impl(FlString text, ImageHandle image, FlWidgetConfig cfg) {
    FlWidgetVariant variant = cfg.variant != 0 ? cfg.variant : FlWidgetVariant_Ghost;

    UiWidgetState w = ui_widget_prepare(text, cfg, STANDARD_WIDGET_FLAGS, variant, /*animate_text=*/true);
    FlSignal signal = w.signal;
    FlColor bg_color = w.colors.bg;
    FlColor text_color = w.colors.text;
    FlColor border_color = w.colors.border;
    FlLayoutId button_id = w.id;

    ui_layout_area_id(button_id,
          .sizing = { sizing_fixed(UI_BUTTON_WITH_IMAGE_WIDTH), sizing_fixed(UI_BUTTON_WITH_IMAGE_HEIGHT) },
          .child_gap = FlSpacing_MD,
          .child_alignment = ui_align(Left, Center),
          .padding = fl_padding_hv(FlSpacing_XL, FlSpacing_SM),
          .radius = corner_radius(FlRadius_MD),
          .color = bg_color,
          .direction = LayoutDirection_LeftToRight,
          .border = fl_border(border_color, FlBorderWidth_Medium))
    {
        ui_image(image, .color = text_color);
        ui_text_styled_impl(text, (FlTextConfig) { .line_height = 34, .color = text_color });
    }

    return signal;
}
// clang-format on

// Determinate progress bar: a track that grows to fill its width, with a fill rect sized to a caller-owned
// fraction of the track. Pure paint - no id, no behavior pass, no animation.

void ui_progress_bar_impl(float fraction, FlProgressConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    if (fraction < 0.0f) {
        fraction = 0.0f;
    } else if (fraction > 1.0f) {
        fraction = 1.0f;
    }

    FlThemeColors theme_scratch;
    const FlThemeColors* theme = ui_resolve_widget_theme(self, cfg.widget.theme_override, &theme_scratch);

    const float track_height = 8.0f; // matches the slider track height

    FlColor track_color = theme->surface.elevated;
    FlColor fill_color = cfg.widget.color.a > 0 ? cfg.widget.color : theme->interactive.base;

    ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(track_height) }, .radius = corner_radius(FlRadius_Full),
                   .color = track_color) {
        // sizing_percent is 0..1. A zero fraction emits no fill rect - an empty bar is just the bare track.
        if (fraction > 0.0f) {
            ui_layout_area(.sizing = { sizing_percent(fraction), sizing_fixed(track_height) },
                           .radius = corner_radius(FlRadius_Full), .color = fill_color) {}
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Link widget - clickable text with underline for hyperlinks

FlSignal ui_link_impl(FlString text, FlLinkConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    // Link is custom-coloured (it derives its own colours below).
    UiWidgetState w = ui_widget_begin(text, cfg.widget, STANDARD_WIDGET_FLAGS);
    FlLayoutId layout_id = w.id;
    FlSignal signal = w.signal;
    ItemState* item = w.item;
    const FlThemeColors* theme = &w.theme;

    FlColor base_link_color = cfg.widget.text_color.a > 0 ? cfg.widget.text_color : theme->interactive.base;
    FlColor link_color = base_link_color;

    FlColor bg_color = UI_HIT_TEST_FILL(2);
    FlColor border_color = { 0, 0, 0, 0 };

    if (item && item->state.focused_t > 0.0f) {
        FlColor focus_bg
            = { (u8)(base_link_color.r / 4), (u8)(base_link_color.g / 4), (u8)(base_link_color.b / 4), 220 };
        bg_color = ui_color_lerp(bg_color, focus_bg, item->state.focused_t);
        border_color = ui_color_lerp(border_color, base_link_color, item->state.focused_t);
        FlColor white = { 255, 255, 255, 255 };
        link_color = ui_color_lerp(base_link_color, white, item->state.focused_t);
    }

    // Brighten on hover, but not while focused (the focused text is already white).
    if (item && item->state.hot_t > 0.0f && item->state.focused_t < 0.5f) {
        FlColor hover_color = { (u8)min_i32_impl(link_color.r + 40, 255), (u8)min_i32_impl(link_color.g + 40, 255),
                                (u8)min_i32_impl(link_color.b + 40, 255), link_color.a };
        link_color = ui_color_lerp(link_color, hover_color, item->state.hot_t);
    }

    u16 font_size
        = cfg.font_size > 0 ? cfg.font_size : font_cache_get_size(self->shared->font_cache, self->active_font);

    float underline_height = font_size >= 28 ? 2.0f : 1.0f;

    // The underline fades in with hover/focus and is invisible at rest.
    FlColor underline_color = link_color;
    if (item) {
        float interact_t = max_f32_impl(item->state.focused_t, item->state.hot_t);
        underline_color.a = (u8)(link_color.a * interact_t);
    } else {
        underline_color.a = 0;
    }

    float padding = 4.0f;

    // clang-format off
    ui_layout_area_id(layout_id,
        .sizing = { sizing_fit(0), sizing_fit(0) },
        .direction = LayoutDirection_TopToBottom,
        .padding = padding_all((u16)padding),
        .radius = corner_radius(FlRadius_SM),
        .color = bg_color,
        .border = fl_border(border_color, FlBorderWidth_Thin))
    {
        ui_text_styled_impl(text, (FlTextConfig){
            .font_size = font_size,
            .color = link_color,
            .valign = FlTextVAlign_Center,
            .no_wrap = true
        });

        ui_layout_area(
            .sizing = { sizing_grow(0), sizing_fixed(underline_height) },
            .color = underline_color) {}
    }
    // clang-format on

    return signal;
}
