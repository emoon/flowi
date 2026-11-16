#include "ui.h"
#include "ui_private.h"
#include "ui_widget_internal.h"
#include <core/core.h>

static void navigate_from_tab(const UiTabStrip* strip, int i) {
    FlUiCtx* self = g_ui_ctx;
    const bool vertical = strip->cfg.vertical;
    bool* nav_prev = vertical ? &self->nav_frame.nav_up : &self->nav_frame.nav_left;
    bool* nav_next = vertical ? &self->nav_frame.nav_down : &self->nav_frame.nav_right;

    if (*nav_prev && i > 0) {
        fl_set_focus(ui_tab_strip_tab_id(strip, i - 1));
        *nav_prev = false;
    } else if (*nav_next && i < strip->count - 1) {
        fl_set_focus(ui_tab_strip_tab_id(strip, i + 1));
        *nav_next = false;
    }
}

// Keyboard navigation is gated on the same value the tabs registered focus under.
void ui_tab_strip_navigate(const UiTabStrip* strip) {
    if (strip->cfg.widget.focusable == FlFocusable_No) {
        return;
    }
    for_count(i, strip->count) {
        ItemState* item = get_item_state(ui_tab_strip_tab_id(strip, i));
        if (item && item->state.focused_t > 0.5f) {
            navigate_from_tab(strip, i);
            return;
        }
    }
}
