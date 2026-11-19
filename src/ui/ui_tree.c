#include "ui_tree.h"
#include "list/list_private.h"
#include <core/arena.h>
#include <flowi/ui/item_behavior.h>
#include <ui/ui_theme.h>
#include "layout.h"
#include "painter.h"
#include "tree_flat.h"
#include "ui.h"
#include "ui_widget_internal.h"

#include <math.h>

#define TREE_DEFAULT_INDENT 16.0f
#define TREE_ARROW_STEPS 4
#define TREE_ARROW_LENGTH 4.0f

typedef struct TreeVirtualState {
    u32 row_count;
    u32 toggle_row;
    u32 toggle_subtree;
    bool toggle_pending;
    bool toggle_expanded;
    bool seen;
} TreeVirtualState;

struct FlTreeContext {
    FlListContext* list;
    const FlTreeRow* rows;
    TreeFlat flat;
    u64 flat_frame;
    bool is_virtual;
    u32 count;
    f32 indent;
    u64 toggled_id;
    u32 toggled_row;
    bool toggle_expanded;
    bool row_skipped;
    hashmap(u32, TreeVirtualState) virtual_states;
};

FlTreeContext* ui_tree_create(FlArena* arena, FlListContext* list) {
    FlTreeContext* self = arena_alloc_zero(arena, FlTreeContext);
    self->list = list;
    hashmap_new(&self->virtual_states, arena, 8);
    return self;
}

static FlTreeRow tree_row(const FlTreeContext* self, u32 row) {
    if (!self->is_virtual) {
        return self->rows[row];
    }
    FlTreeVirtualRow at = tree_flat_row(&self->flat, row);
    return (FlTreeRow) { .id = (u64)row + 1, .depth = at.depth, .has_children = true, .expanded = at.node >= 0 };
}

static u32 subtree_end(const FlTreeContext* self, u32 row) {
    if (self->is_virtual) {
        return tree_flat_subtree_end(&self->flat, row);
    }
    u32 end = row + 1;
    while (end < self->count && self->rows[end].depth > self->rows[row].depth) {
        end++;
    }
    return end;
}

static u32 parent_row(const FlTreeContext* self, u32 row) {
    if (self->is_virtual) {
        return tree_flat_parent_row(&self->flat, row);
    }
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
        st->focus_cursor_id = tree_row(self, row).id;
    }
}

static void toggle_row(FlTreeContext* self, u32 row) {
    FlTreeRow r = tree_row(self, row);
    self->toggled_id = r.id;
    self->toggled_row = row;
    self->toggle_expanded = !r.expanded;
    if (r.expanded) {
        move_cursor_out_of_subtree(self, row);
    }
}

static i32 step_into(FlTreeContext* self, u32 row) {
    FlTreeRow r = tree_row(self, row);
    if (!r.has_children) {
        return (i32)row;
    }
    if (!r.expanded) {
        toggle_row(self, row);
        return (i32)row;
    }
    return subtree_end(self, row) > row + 1 ? (i32)row + 1 : (i32)row;
}

static i32 step_out_of(FlTreeContext* self, u32 row) {
    FlTreeRow r = tree_row(self, row);
    if (r.has_children && r.expanded) {
        toggle_row(self, row);
        return (i32)row;
    }
    return (i32)parent_row(self, row);
}

static i32 key_target_row(i32 cursor_row) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    FlNavFrame nav = fl_get_nav_frame();
    if (nav.nav_right) {
        return cursor_row < 0 ? cursor_row : step_into(self, (u32)cursor_row);
    }
    if (nav.nav_left) {
        return cursor_row < 0 ? cursor_row : step_out_of(self, (u32)cursor_row);
    }
    return cursor_row;
}

static FlListRange tree_list_begin(FlTreeContext* self, ListBeginArgs args) {
    args.disable_reorder = true;
    args.key_target_row = key_target_row;
    FlListRange range = list_begin_impl(self->list, args);
    if (self->list->has_kb_focus && self->count > 0) {
        fl_consume_nav_left();
        fl_consume_nav_right();
    }
    return range;
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
    painter_rects(g_ui_ctx->painter, bars, TREE_ARROW_STEPS);
}

static void arrow_slot(FlTreeContext* self, u32 row, bool expanded) {
    FlListContext* list = self->list;
    f32 height = list->geometry.row_height;
    FlLayoutId arrow_id = list_layout_id(list->current_id.name, LIST_ID_TREE_ARROW(row));

    fl_layout_start(arrow_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(self->indent), sizing_fixed(height) },
        .color = UI_HIT_TEST_FILL(1),
    });
    FlSignal signal = fl_item_behavior(arrow_id, FlItemFlags_clickable);
    if (signal.pressed) {
        list_claim_press(list);
        toggle_row(self, row);
    }
    draw_arrow(expanded, self->indent, height);
    fl_layout_end();
}

static void row_lead(FlTreeContext* self, u32 row, FlTreeRow r) {
    u32 blank_levels = r.has_children ? r.depth : (u32)r.depth + 1;
    if (blank_levels > 0) {
        ui_spacer_h(self->indent * (f32)blank_levels);
    }
    if (r.has_children) {
        arrow_slot(self, row, r.expanded);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// With columns, the lead is cut off at the first column's right edge so the other columns stay under their headers.
static void row_lead_in_column(FlTreeContext* self, u32 row, FlTreeRow r) {
    FlListContext* list = self->list;
    if (list->column_count == 0) {
        row_lead(self, row, r);
        return;
    }
    f32 lead = self->indent * (f32)(r.depth + 1);
    f32 width = lead < list->column_widths[0] ? lead : list->column_widths[0];
    list->cell_cursor = width;
    if (width <= 0.0f) {
        return;
    }
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(width), sizing_fixed(list->geometry.row_height) },
        .direction = LayoutDirection_LeftToRight,
        .clip = { .horizontal = true },
    });
    row_lead(self, row, r);
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlSignal row_begin(FlTreeContext* self, u32 row, FlTreeRow r) {
    FlSignal signal = list_item_begin_impl(self->list, r.id);
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fixed(self->list->geometry.row_height) },
        .direction = LayoutDirection_LeftToRight,
        .child_alignment = { .y = LayoutAlignmentY_Center },
    });
    row_lead_in_column(self, row, r);
    return signal;
}

static void tree_reset_frame(FlTreeContext* self, f32 indent) {
    self->indent = indent > 0.0f ? indent : TREE_DEFAULT_INDENT;
    self->toggled_id = 0;
    self->toggled_row = 0;
    self->toggle_expanded = false;
    self->row_skipped = false;
}

FlListRange fl_tree_begin(FlLayoutId id, const FlTreeRow* rows, uint32_t count, FlSelectionSet* set,
                          FlTreeConfig config) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    self->is_virtual = false;
    self->rows = rows;
    self->count = rows != nullptr ? count : 0;
    tree_reset_frame(self, config.indent);

    return tree_list_begin(self, (ListBeginArgs) {
                                     .id = id,
                                     .rows = list_rows_from_tree(rows, config.list.labels, count),
                                     .set = set,
                                     .config = config.list,
                                 });
}

static TreeVirtualState* virtual_state_get(FlTreeContext* self, u32 clay_id) {
    TreeVirtualState* state = hashmap_get(&self->virtual_states, clay_id);
    if (state == nullptr) {
        TreeVirtualState fresh = { 0 };
        state = hashmap_insert(&self->virtual_states, clay_id, fresh);
    }
    FL_ASSERT(state != nullptr);
    return state;
}

typedef struct RowShift {
    u64 after_id;
    u64 removed;
    u64 added;
} RowShift;

static u64 shifted_id(RowShift shift, u64 id) {
    if (id <= shift.after_id) {
        return id;
    }
    if (id - shift.after_id <= shift.removed) {
        return 0;
    }
    return id - shift.removed + shift.added;
}

static u32 rows_under(const FlTreeContext* self, u32 row) {
    return subtree_end(self, row) - row - 1;
}

static bool toggle_explains(const FlTreeContext* self, const TreeVirtualState* state) {
    bool row_exists = state->toggle_pending && state->toggle_row < self->count;
    if (!row_exists || tree_row(self, state->toggle_row).expanded != state->toggle_expanded) {
        return false;
    }
    i64 rows_delta = (i64)self->count - (i64)state->row_count;
    i64 subtree_delta = (i64)rows_under(self, state->toggle_row) - (i64)state->toggle_subtree;
    return rows_delta == subtree_delta;
}

static RowShift shift_for(const FlTreeContext* self, const TreeVirtualState* state) {
    if (!toggle_explains(self, state)) {
        return (RowShift) { .removed = UINT64_MAX };
    }
    return (RowShift) {
        .after_id = (u64)state->toggle_row + 1,
        .removed = state->toggle_subtree,
        .added = rows_under(self, state->toggle_row),
    };
}

static void follow_row_change(FlTreeContext* self, u32 clay_id, FlSelectionSet* set) {
    TreeVirtualState* state = virtual_state_get(self, clay_id);
    bool changed = state->seen && state->row_count != self->count;
    if (!changed) {
        return;
    }
    ListState* st = list_state_get(self->list, clay_id);
    RowShift shift = shift_for(self, state);
    if (!st->focus_seed_pending) {
        st->focus_cursor_id = shifted_id(shift, st->focus_cursor_id);
    }
    st->anchor_id = shifted_id(shift, st->anchor_id);
    if (set != nullptr) {
        selection_set_shift(set, shift.after_id, shift.removed, shift.added);
    }
}

FlListRange fl_tree_begin_virtual(FlLayoutId id, uint32_t root_count, const FlTreeExpanded* expanded, uint32_t count,
                                  FlSelectionSet* set, FlTreeConfig config) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    self->flat = tree_flat_build(g_ui_ctx->render_frame_allocator, root_count, expanded, count);
    self->is_virtual = true;
    self->flat_frame = g_ui_ctx->frame_counter;
    self->rows = nullptr;
    self->count = self->flat.row_count;
    tree_reset_frame(self, config.indent);
    follow_row_change(self, ui_layout_id_to_clay_id(id), set);

    return tree_list_begin(self, (ListBeginArgs) {
                                     .id = id,
                                     .rows = list_rows_from_count(self->count),
                                     .set = set,
                                     .config = config.list,
                                 });
}

FlTreeVirtualRow fl_tree_row_at(uint32_t row) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    bool this_frame = self->is_virtual && self->flat_frame == g_ui_ctx->frame_counter;
    bool has_row = this_frame && row < self->count;
    FL_ASSERT(has_row);
    if (!has_row) {
        return (FlTreeVirtualRow) { .parent = -1, .node = -1 };
    }
    return tree_flat_row(&self->flat, row);
}

static bool row_missing(FlTreeContext* self, u32 row) {
    FL_ASSERT(row < self->count);
    self->row_skipped = row >= self->count;
    return self->row_skipped;
}

FlSignal fl_tree_item_begin(uint32_t row) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    if (row_missing(self, row)) {
        return (FlSignal) { 0 };
    }
    return row_begin(self, row, tree_row(self, row));
}

FlSignal fl_tree_item_begin_virtual(uint32_t row, bool has_children) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    if (row_missing(self, row)) {
        return (FlSignal) { 0 };
    }
    FlTreeRow r = tree_row(self, row);
    r.has_children = has_children || r.expanded;
    return row_begin(self, row, r);
}

void fl_tree_item_end(void) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    if (self->row_skipped) {
        self->row_skipped = false;
        return;
    }
    fl_layout_end();
    list_item_end_impl(self->list);
}

static void remember_virtual_frame(FlTreeContext* self) {
    TreeVirtualState* state = virtual_state_get(self, self->list->current_clay_id);
    state->seen = true;
    state->row_count = self->count;
    state->toggle_pending = self->toggled_id != 0;
    state->toggle_row = self->toggled_row;
    state->toggle_expanded = self->toggle_expanded;
    state->toggle_subtree = state->toggle_pending ? rows_under(self, self->toggled_row) : 0;
}

FlTreeResult fl_tree_end(void) {
    FlTreeContext* self = g_ui_ctx->ui_tree_ctx;
    if (self->is_virtual) {
        remember_virtual_frame(self);
    }
    return (FlTreeResult) {
        .list = list_end_impl(self->list),
        .toggled_id = self->toggled_id,
        .toggle_expanded = self->toggle_expanded,
        .toggled_row = self->toggled_row,
    };
}
