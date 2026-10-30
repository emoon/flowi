#pragma once

#include "external/clay/clay.h"
#include "ui_private.h"

// Walk one frame's render commands, maintaining the scissor stack, and refresh every registered interactive
// item's rect, visibility and hot/active/focus state from it. Sets self->next_hot_id; the caller promotes it to
// hot_id after resolving the mouse edges. Items are stamped with the shared item epoch so the ones no window
// touched can be retired once the pass ends.
void ui_hit_test_frame(FlUiCtx* self, Clay_RenderCommandArray commands, float dt);

// True when the point lies in the item's rect cut by the clip chain it was last emitted under.
bool ui_item_hit_contains(const ItemState* item, Vec2 point);
