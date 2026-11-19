#include "ui_message_box.h"
#include "layout.h"
#include "ui.h"
#include "ui_private.h"
#include <flowi/ui/modal.h>

// Default panel width in logical px, wide enough for a short prompt and a two-button row.
#define UI_MESSAGE_BOX_DEFAULT_WIDTH 400.0f
// Dim full-screen scrim, semi-transparent so the background stays faintly visible behind the dialog.
#define UI_MESSAGE_BOX_SCRIM_COLOR ((FlColor) { 0, 0, 0, 160 })

// Fixed scope id for the modal focus/input-blocking machinery; the bytes spell 'msgb'.
#define UI_MESSAGE_BOX_SCOPE_ID 0x6D736762u

// Fixed internal element ids; the buttons carry these via id_override.
#define UI_MESSAGE_BOX_BACKDROP_ID "##msgbox_backdrop"
#define UI_MESSAGE_BOX_OK_ID "##msgbox_ok"
#define UI_MESSAGE_BOX_CANCEL_ID "##msgbox_cancel"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    FlSignal ok;
    FlSignal cancel;
} MessageBoxSignals;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool message_box_has_cancel(const FlMessageBoxConfig* cfg) {
    return cfg->cancel_label.length > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Right-aligned button row: [Cancel] [OK], primary rightmost.
static void message_box_button_row(const FlMessageBoxConfig* cfg, MessageBoxSignals* signals) {
    const FlString confirm_label = cfg->confirm_label.length > 0 ? cfg->confirm_label : FL_S("OK");

    ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }, .direction = LayoutDirection_LeftToRight,
                   .child_gap = FlSpacing_SM, .child_alignment = ui_align(Right, Center)) {
        if (message_box_has_cancel(cfg)) {
            signals->cancel = ui_button_impl(cfg->cancel_label,
                                             (FlWidgetConfig) { .id_override = FL_S(UI_MESSAGE_BOX_CANCEL_ID),
                                                                .variant = FlWidgetVariant_Outline });
        }
        signals->ok = ui_button_impl(confirm_label, (FlWidgetConfig) { .id_override = FL_S(UI_MESSAGE_BOX_OK_ID),
                                                                       .variant = FlWidgetVariant_Solid });
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static MessageBoxSignals message_box_panel(FlString title, FlString message, const FlMessageBoxConfig* cfg,
                                           const FlThemeColors* theme) {
    MessageBoxSignals signals = { 0 };
    const float width = cfg->width > 0.0f ? cfg->width : UI_MESSAGE_BOX_DEFAULT_WIDTH;

    ui_layout_area(.sizing = { sizing_fixed(width), sizing_fit() }, .direction = LayoutDirection_TopToBottom,
                   .child_gap = FlSpacing_MD, .padding = padding_all(FlSpacing_LG), .color = theme->surface.elevated,
                   .radius = corner_radius(FlRadius_LG),
                   .border = fl_border(theme->border.default_color, FlBorderWidth_Thin)) {
        ui_text_styled_impl(title, (FlTextConfig) { .color = theme->text.primary });
        ui_text_styled_impl(message, (FlTextConfig) { .color = theme->text.secondary });
        message_box_button_row(cfg, &signals);
    }
    return signals;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Root-attached floating backdrop that captures pointer events.
static MessageBoxSignals message_box_backdrop(FlString title, FlString message, const FlMessageBoxConfig* cfg,
                                              const FlThemeColors* theme) {
    MessageBoxSignals signals = { 0 };
    const i16 z_index = cfg->z_index != 0 ? cfg->z_index : UI_Z_OVERLAY;
    const FlLayoutId backdrop_id = { .name = FL_S(UI_MESSAGE_BOX_BACKDROP_ID), .id = 0 };

    ui_floating_area_id(backdrop_id, .attach_to = FlFloatingAttachTo_Root,
                        .pointer_capture = FlFloatingPointerCapture_Capture, .z_index = z_index,
                        .sizing = { sizing_grow(), sizing_grow() }) {
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = UI_MESSAGE_BOX_SCRIM_COLOR,
                       .child_alignment = ui_align(Center, Center)) {
            // Scope the panel's interactive widgets to the modal so they receive input while the rest is blocked.
            fl_modal_begin(UI_MESSAGE_BOX_SCOPE_ID);
            signals = message_box_panel(title, message, cfg, theme);
            fl_modal_end();
        }
    }
    return signals;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Skip dismissal on the opening frame so the input that opened the dialog can't also close it (the button's
// own just_gained_focus guard already blocks the opening keypress from confirming via OK).
static FlDialogResult message_box_dismissal(const FlMessageBoxConfig* cfg, const MessageBoxSignals* signals) {
    if (signals->ok.clicked) {
        return FlDialogResult_Confirmed;
    }
    if (message_box_has_cancel(cfg) && signals->cancel.clicked) {
        return FlDialogResult_Cancelled;
    }
    // ESC always cancels, including single-button alerts, where it is the keyboard path to dismissal.
    if (ui_key_pressed(FlKey_Escape)) {
        return FlDialogResult_Cancelled;
    }
    return FlDialogResult_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A closed dialog is a no-op; if the app cleared `*open` itself, close the modal scope still armed from the
// previous frame.
static void message_box_close_stale_scope(const FlUiCtx* ctx) {
    if (ctx->popup_focus_scope_id == UI_MESSAGE_BOX_SCOPE_ID) {
        fl_modal_close();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// On the opening frame, claim the initial-focus request and focus the primary (OK) button.
static bool message_box_claim_opening_frame(FlUiCtx* ctx) {
    const bool opening_frame = ctx->popup_needs_initial_focus;
    if (opening_frame) {
        ctx->popup_needs_initial_focus = false;
        fl_set_focus((FlLayoutId) { .name = FL_S(UI_MESSAGE_BOX_OK_ID), .id = 0 });
    }
    return opening_frame;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlDialogResult ui_message_box_impl(FlString title, FlString message, bool* open, FlMessageBoxConfig cfg) {
    FlUiCtx* ctx = g_ui_ctx;

    if (open == nullptr || !*open) {
        message_box_close_stale_scope(ctx);
        return FlDialogResult_None;
    }

    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    // Input to widgets outside the dialog is blocked from here on.
    fl_modal_begin_frame(UI_MESSAGE_BOX_SCOPE_ID);

    // The dialog owns its own dismissal (ESC / buttons); disable popup auto-close for this scope.
    ctx->popup_auto_close = false;

    const bool opening_frame = message_box_claim_opening_frame(ctx);
    const MessageBoxSignals signals = message_box_backdrop(title, message, &cfg, theme);

    const FlDialogResult result = opening_frame ? FlDialogResult_None : message_box_dismissal(&cfg, &signals);
    if (result != FlDialogResult_None) {
        *open = false;
        fl_modal_close();
    }

    return result;
}
