#pragma once

#include <ui/ui_types.h>     // FlString/bool - base types the generated headers below rely on
#include <flowi/ui/layout.h> // @generated FlLayoutId (api_gen)
#include <flowi/ui/popup.h>  // @generated FlTooltipConfig (api_gen)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tooltip API
//
// A floating hover-dwell label. Call AFTER the anchor widget has been emitted, passing the anchor's layout id.
// The tooltip shows once the pointer has dwelled over the anchor for cfg.delay seconds (the mouse stationary
// timer) and hides when the anchor is no longer hovered. It rides the floating layer, so it escapes the clip
// stack and z-orders above siblings; it is non-capturing and never blocks input to the anchor.
void ui_tooltip_impl(FlLayoutId anchor_id, FlString text, FlTooltipConfig cfg);

// clang-format off
#define ui_tooltip(anchor_id, text, ...) ui_tooltip_impl(anchor_id, text, (FlTooltipConfig) { __VA_ARGS__ })
// clang-format on
