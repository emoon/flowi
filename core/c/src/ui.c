#include <flowi_core/math_data.h>
#include <flowi_core/ui.h>
#include "internal.h"

// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos) {
    ctx->cursor = pos;
}
