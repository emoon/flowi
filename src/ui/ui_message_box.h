#pragma once

#include <ui/ui_types.h>     // FlString/bool - base types the generated headers below rely on
#include <flowi/ui/layout.h> // @generated FlLayoutId (api_gen)
#include <flowi/ui/popup.h>  // @generated FlPopupConfig / FlMessageBoxConfig / FlDialogResult (api_gen)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Message-box / confirm-dialog API
//
// A modal confirm/alert dialog: a dim, pointer-capturing full-screen backdrop with a centered panel holding a
// title, message, and one or two buttons. The app owns a single `bool* open`; the function early-returns
// FlDialogResult_None when `!*open`, so it can be called UNCONDITIONALLY every frame. While open it returns None;
// on dismissal it returns the decision for that one frame and clears `*open`.
//
// Call it BEFORE the main content so the opening frame blocks background input (steady-state blocking is airtight
// regardless of order).
//
//   OK button confirms; the Cancel button or ESC (always, including single-button alerts) cancels. The backdrop
//   captures the pointer but does not dismiss on outside-click. Initial focus is on the primary (OK) button; the
//   buttons navigate left/right and activate with Enter/Space/Gamepad-A.
//
//   cfg.cancel_label presence encodes the button count: empty => single-button alert; set => two-button confirm.
FlDialogResult ui_message_box_impl(FlString title, FlString message, bool* open, FlMessageBoxConfig cfg);

// clang-format off
#define ui_message_box(title, message, open, ...) \
    ui_message_box_impl(title, message, open, (FlMessageBoxConfig) { __VA_ARGS__ })
// clang-format on
