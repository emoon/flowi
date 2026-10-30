#pragma once

#include <core/types.h>
#include <ui/ui_types.h>
#include <flowi/core/platform.h>
#include <flowi/ui/layout.h> // @generated FlLayoutId (api_gen)
#include <flowi/ui/popup.h>  // @generated FlPopupConfig / FlMessageBoxConfig / FlDialogResult (api_gen)

typedef struct FlUiCtx FlUiCtx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Popup API

// Begin a popup scope - content declared within this scope will be rendered after main UI
void ui_popup_begin(FlLayoutId anchor_id, FlPopupConfig config);

void ui_popup_end(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal API (used by ui system)

// Check for auto-close triggers (ESC, B button)
void ui_popup_check_auto_close(FlUiCtx* ctx);

// Close the active popup (restores focus, clears scope)
void ui_popup_close_internal(FlUiCtx* ctx);

// Claim the popup focus scope for scope_id, saving the focus it displaces. Without take_over the scope is claimed
// only when none is armed; with take_over it replaces any other armed scope. Returns true when the focus was saved
// (the scope was newly claimed).
bool ui_focus_scope_arm(FlUiCtx* ctx, u32 scope_id, bool take_over);

// Hand focus back to the saved widget and drop the armed scope and the current popup scope. Callers add their own
// resets (saved_focused_id, popup_open_frame, ...).
void ui_focus_scope_release(FlUiCtx* ctx);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scope-based macro (follows ui_layout_area pattern)

static int UI_POPUP_LATCH UNUSED_VARIABLE = 0;

// clang-format off
#define ui_popup(anchor_id, ...)  \
    for ( \
         UI_POPUP_LATCH = (ui_popup_begin(anchor_id, (FlPopupConfig) { __VA_ARGS__ }), 0); \
         UI_POPUP_LATCH < 1; \
         UI_POPUP_LATCH = 1, ui_popup_end())
// clang-format on
