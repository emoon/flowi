#include "ui_tooltip.h"
#include "layout.h"
#include "ui.h"
#include "ui_private.h"
#include "ui_tokens.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Defaults

// Seconds the pointer must dwell over the anchor before the tooltip shows.
#define UI_TOOLTIP_DEFAULT_DELAY 0.5f
// Floating offset below the anchor's bottom-left.
#define UI_TOOLTIP_DEFAULT_OFFSET_Y 4.0f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_tooltip_impl(FlLayoutId anchor_id, FlString text, FlTooltipConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();

    // Shown while the anchor is present and hovered and the pointer has been stationary for at least delay seconds.
    FlElementState anchor = ui_get_element_state(anchor_id);
    if (!anchor.found || !anchor.hot) {
        return;
    }

    float delay = cfg.delay > 0.0f ? cfg.delay : UI_TOOLTIP_DEFAULT_DELAY;
    if (ctx->input_state.mouse.stationary_timer < delay) {
        return;
    }

    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    u32 anchor_clay_id = ui_layout_id_to_clay_id(anchor_id);

    // The floating element's id: the anchor's name with the anchor's hash as sub-id.
    FlLayoutId tip_id = { .name = anchor_id.name, .id = (int)anchor_clay_id };

    FlVec2 offset = cfg.offset;
    if (offset.x == 0.0f && offset.y == 0.0f) {
        offset.y = UI_TOOLTIP_DEFAULT_OFFSET_Y;
    }
    i16 z_index = cfg.z_index != 0 ? cfg.z_index : UI_Z_OVERLAY;

    // Passthrough floating label attached under the anchor.
    ui_floating_area_id(tip_id, .attach_to = FlFloatingAttachTo_Element, .parent_id = anchor_clay_id,
                        .pointer_capture = FlFloatingPointerCapture_Passthrough, .z_index = z_index, .offset = offset,
                        .element_attach = FlFloatingAttachPoint_LeftTop,
                        .parent_attach = FlFloatingAttachPoint_LeftBottom) {
        ui_layout_area(.padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .color = theme->surface.overlay,
                       .radius = corner_radius(FlRadius_SM),
                       .border = fl_border(theme->border.default_color, FlBorderWidth_Thin)) {
            ui_text_styled_impl(text, (FlTextConfig) { .color = theme->text.primary, .no_wrap = true });
        }
    }
}
