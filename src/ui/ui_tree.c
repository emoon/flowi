#include "ui_tree.h"
#include "list/list_private.h"
#include <core/arena.h>
#include <flowi/ui/item_behavior.h>
#include <ui/ui_theme.h>
#include "layout.h"
#include "painter.h"
#include "ui.h"

#include <math.h>

#define TREE_DEFAULT_INDENT 16.0f
#define TREE_ARROW_STEPS 4
#define TREE_ARROW_LENGTH 4.0f

struct FlTreeContext {
    FlListContext* list;
    const FlTreeRow* rows;
    u32 count;
    f32 indent;
    u64 toggled_id;
    bool toggle_expanded;
};

FlTreeContext* ui_tree_create(FlArena* arena) {
    return arena_alloc_zero(arena, FlTreeContext);
}

static u32 subtree_end(const FlTreeContext* self, u32 row) {
    u32 end = row + 1;
    while (end < self->count && self->rows[end].depth > self->rows[row].depth) {
        end++;
    }
    return end;
}

static u32 parent_row(const FlTreeContext* self, u32 row) {
    u32 i = row;
    while (i > 0) {
        i--;
        if (self->rows[i].depth < self->rows[row].depth) {
            return i;
        }
    }
    return row;
}

static void move_cursor_out_of_subtree(const FlTreeContext* self, u32 row) {
    FlListContext* list = self->list;
    ListState* st = list_state_get(list, list->current_clay_id);
    i32 cursor = list_index_of_id(list, st->focus_cursor_id);
    bool below_row = cursor > (i32)row;
    if (below_row && (u32)cursor < subtree_end(self, row)) {
        st->focus_cursor_id = self->rows[row].id;
    }
}

static void toggle_row(FlTreeContext* self, u32 row) {
    const FlTreeRow* r = &self->rows[row];
    self->toggled_id = r->id;
    self->toggle_expanded = !r->expanded;
    if (r->expanded) {
        move_cursor_out_of_subtree(self, row);
    }
}

static i32 step_into(FlTreeContext* self, u32 row) {
    const FlTreeRow* r = &self->rows[row];
    if (!r->has_children) {
        return (i32)row;
    }
    if (!r->expanded) {
        toggle_row(self, row);
        return (i32)row;
    }
    return subtree_end(self, row) > row + 1 ? (i32)row + 1 : (i32)row;
}

static i32 step_out_of(FlTreeContext* self, u32 row) {
    const FlTreeRow* r = &self->rows[row];
    if (r->has_children && r->expanded) {
        toggle_row(self, row);
        return (i32)row;
    }
    return (i32)parent_row(self, row);
}

static i32 key_target_row(void* user, i32 cursor_row) {
    FlTreeContext* self = user;
    FlNavFrame nav = fl_get_nav_frame();
    if (nav.nav_right) {
        fl_consume_nav_right();
        return cursor_row < 0 ? cursor_row : step_into(self, (u32)cursor_row);
    }
    if (nav.nav_left) {
        fl_consume_nav_left();
        return cursor_row < 0 ? cursor_row : step_out_of(self, (u32)cursor_row);
    }
    return cursor_row;
}

static void draw_arrow(bool expanded, f32 width, f32 height) {
    FlColor color = fl_theme_colors()->text.secondary;
    f32 left = floorf(width * 0.5f) - (expanded ? TREE_ARROW_LENGTH : TREE_ARROW_STEPS * 0.5f);
    f32 top = floorf(height * 0.5f) - (expanded ? TREE_ARROW_STEPS * 0.5f : TREE_ARROW_LENGTH);
    FlColoredRect bars[TREE_ARROW_STEPS];
    for_count(i, TREE_ARROW_STEPS) {
        f32 step = (f32)i;
        f32 span = (TREE_ARROW_LENGTH - step) * 2.0f;
        bars[i] = (FlColoredRect) {
            .x = left + step,
            .y = top + step,
            .w = expanded ? span : 1.0f,
            .h = expanded ? 1.0f : span,
            .color = color,
        };
    }
    painter_rects(ui_get_painter(), bars, TREE_ARROW_STEPS);
}

static void arrow_slot(FlTreeContext* self, u32 row) {
    FlListContext* list = self->list;
    f32 height = list->current_row_height;
    FlLayoutId arrow_id = { .name = list->current_id.name, .id = -(i32)(row + 1) };

    fl_layout_start(arrow_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(self->indent), sizing_fixed(height) },
        .clip = { .horizontal = true, .vertical = true },
    });
    FlSignal signal = fl_item_behavior(arrow_id, FlItemFlags_clickable);
    if (signal.pressed) {
        list_claim_press(list);
        toggle_row(self, row);
    }
    draw_arrow(self->rows[row].expanded, self->indent, height);
    fl_layout_end();
}

static void row_lead(FlTreeContext* self, u32 row) {
    const FlTreeRow* r = &self->rows[row];
    u32 blank_levels = r->has_children ? r->depth : (u32)r->depth + 1;
    if (blank_levels > 0) {
        ui_spacer_h(self->indent * (f32)blank_levels);
    }
    if (r->has_children) {
        arrow_slot(self, row);
    }
}

FlListRange ui_tree_begin(FlLayoutId id, const FlTreeRow* rows, u32 count, FlSelectionSet* set, FlTreeConfig config) {
    FlTreeContext* self = ui_get_ctx()->ui_tree_ctx;
    self->list = ui_get_ctx()->ui_list_ctx;
    self->rows = rows;
    self->count = rows != nullptr ? count : 0;
    self->indent = config.indent > 0.0f ? config.indent : TREE_DEFAULT_INDENT;
    self->toggled_id = 0;
    self->toggle_expanded = false;

    return list_begin_impl(self->list, (ListBeginArgs) {
                                           .id = id,
                                           .rows = list_rows_from_tree(rows, config.list.labels, count),
                                           .set = set,
                                           .config = config.list,
                                           .key_hook = { .user = self, .target_row = key_target_row },
                                       });
}

FlSignal ui_tree_item_begin(u32 row) {
    FlTreeContext* self = ui_get_ctx()->ui_tree_ctx;
    FL_ASSERT(row < self->count);
    const FlTreeRow* r = &self->rows[row];

    FlSignal signal = list_item_begin_impl(self->list, r->id);
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fit() },
        .direction = LayoutDirection_LeftToRight,
        .child_alignment = { .y = LayoutAlignmentY_Center },
    });
    row_lead(self, row);
    return signal;
}

void ui_tree_item_end(void) {
    FlTreeContext* self = ui_get_ctx()->ui_tree_ctx;
    fl_layout_end();
    list_item_end_impl(self->list);
}

FlTreeResult ui_tree_end(void) {
    FlTreeContext* self = ui_get_ctx()->ui_tree_ctx;
    return (FlTreeResult) {
        .list = list_end_impl(self->list),
        .toggled_id = self->toggled_id,
        .toggle_expanded = self->toggle_expanded,
    };
}
