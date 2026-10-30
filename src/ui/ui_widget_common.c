// Widget prologue and the shared widget-flag constant. The seam it exposes lives in ui_widget_internal.h.

#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include "ui_variant_helpers.h"
#include <core/core.h>
#include <core/math.h>

const FlItemFlags STANDARD_WIDGET_FLAGS = FlItemFlags_clickable | FlItemFlags_focusable | FlItemFlags_hot_animation
                                          | FlItemFlags_active_animation | FlItemFlags_focus_animation;

// Widget prologue: resolve the id, dispatch the shared interaction behaviour, look up the persisted item-state, and
// resolve the theme (override merged in). ui_widget_prepare adds the state-dependent variant colours.
//   - Variant-coloured widgets (button, button-with-image, dropdown): ui_widget_prepare, read state.colors.
//   - Custom-coloured widgets (checkbox, switch, slider, link, pill/chip): ui_widget_begin, fill from
//     state.item + state.theme.
// Two-pass selection widgets (radio group, tab bar) resolve the selection before any item lays out, so they split
// behaviour and colour passes and use neither.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UiWidgetState ui_widget_begin(FlString label, FlWidgetConfig cfg, FlItemFlags flags) {
    UiWidgetState w = { 0 };
    w.id = resolve_widget_id(label, cfg.id_override);
    w.signal = ui_item_behavior(w.id, flags, cfg);
    w.item = get_item_state(w.id);

    // The theme is copied by value into the result; scratch is local.
    FlThemeColors scratch;
    const FlThemeColors* theme = ui_resolve_widget_theme(g_ui_ctx, cfg.theme_override, &scratch);
    w.theme = *theme;
    return w;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UiWidgetState ui_widget_prepare(FlString label, FlWidgetConfig cfg, FlItemFlags flags, FlWidgetVariant variant,
                                bool animate_text) {
    UiWidgetState w = ui_widget_begin(label, cfg, flags);
    w.colors = fl_resolve_widget_colors(variant, cfg.color, cfg.text_color, w.item, &w.theme, animate_text);
    return w;
}
