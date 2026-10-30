#pragma once

#include "ui_private.h"

// Resolve this frame's focus moves: Tab/Shift+Tab through the scoped focus order, then spatial arrow/D-pad
// navigation, then repair a focused_id whose widget vanished. Runs after hit testing, so it reads the rects this
// frame resolved. Emits the NavMoved/NavBlocked feedback events.
void ui_navigation_update(FlUiCtx* self);
