// Column header for a list with columns: the outer wrapper that holds the header above the scrolled rows, the
// header cells with their click and resize handling, the body cells (Cell::begin) and the x-to-column lookup.
//
// Flowi resolves every column width itself (list_columns.c) and emits each header and body cell at that fixed
// width, so a header and its rows always line up. The width the rows get is last frame's measurement.

#include "list_private.h"
#include "../layout.h"
#include "list_columns.h"
#include "list_uniform.h"
#include "../pixel_snap.h"
#include "../ui.h"
#include <flowi/ui/cell.h>
#include <flowi/ui/cursor.h>
#include <flowi/ui/item_behavior.h>
#include <ui/ui_theme.h>

#define LIST_HEADER_PAD_X 6.0f        // Horizontal padding inside a header or body cell (logical px)
#define LIST_HEADER_HANDLE_WIDTH 6.0f // Width of the resize grab area on a header cell's right edge (logical px)
#define LIST_HEADER_RESIZE_FLOOR 8.0f // A drag never makes a column narrower than this (logical px)
#define LIST_CELL_MIN_WIDTH 0.001f    // Width of a cell with no room left; Clay reads a fixed size of 0 as unbounded

static FlLayoutId header_id(const FlListContext* self, i32 id) {
    return list_layout_id(self->current_id.name, id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 list_column_at(const FlListContext* self, f32 x) {
    f32 edge = 0.0f;
    for_count(i, self->column_count) {
        edge += self->column_widths[i];
        if (x >= 0.0f && x < edge) {
            return self->columns[i].id;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool mouse_in_row(const FlListContext* self, FlElementState row) {
    ListViewport vp;
    if (!list_get_viewport(self, &vp)) {
        return false;
    }
    const FlUiCtx* ui = self->ui_ctx;
    f32 y = ui->input_state.mouse.y;
    bool in_rows = y >= row.pos.y && y < row.pos.y + row.size.y;
    return in_rows && list_viewport_contains(&vp, ui->input_state.mouse.x, y);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 list_row_cell_under_mouse(const FlListContext* self, FlLayoutId row_id) {
    if (self->column_count == 0) {
        return 0;
    }
    FlElementState row = ui_get_element_state(row_id);
    if (!row.found) {
        return 0;
    }
    if (!mouse_in_row(self, row)) {
        return 0;
    }
    const FlUiCtx* ui = self->ui_ctx;
    return list_column_at(self, (ui->input_state.mouse.x - row.pos.x) / ui_scale_or_one(ui->ui_scale));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The dragged column's width: where it was when the press landed plus the drag distance, never below the column's
// minimum or the resize floor.
static f32 resized_width_of(const FlListContext* self, const ListState* st, u32 index, f32 drag_x) {
    f32 width = st->resize_start_width + drag_x / ui_scale_or_one(self->ui_ctx->ui_scale);
    f32 floor = self->columns[index].min_width > LIST_HEADER_RESIZE_FLOOR ? self->columns[index].min_width
                                                                          : LIST_HEADER_RESIZE_FLOOR;
    return width > floor ? width : floor;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void header_resize_arm(FlListContext* self, ListState* st, u32 index) {
    st->resize_column = self->columns[index].id;
    st->resize_start_width = self->column_widths[index];
    st->resize_moved = false;
    self->frame_header_pressed = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Reports the dragged column's new width once the mouse has moved, and ends the drag with the button.
static void header_resize_drag(FlListContext* self, ListState* st, u32 index, FlSignal handle) {
    st->resize_moved = st->resize_moved || handle.drag_delta.x != 0.0f;
    if (handle.dragging && st->resize_moved) {
        self->resized_column = self->columns[index].id;
        self->resized_width = resized_width_of(self, st, index, handle.drag_delta.x);
    }
    if (!self->ui_ctx->input_state.mouse.buttons[FlMouseButton_Left]) {
        st->resize_column = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Records a press on a header edge, or the drag that follows it.
static void header_resize(FlListContext* self, ListState* st, u32 index, FlSignal handle) {
    u32 id = self->columns[index].id;
    if (handle.pressed) {
        header_resize_arm(self, st, index);
    }
    bool resizing = st->resize_column == id;
    if (handle.hovered || resizing) {
        fl_cursor_set((FlCursor) { .shape = FlCursorShape_ResizeH });
    }
    if (resizing) {
        header_resize_drag(self, st, index, handle);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The grab area on the cell's right edge, with the divider line drawn inside it.
static FlSignal header_handle(const FlListContext* self, u32 index) {
    const FlThemeColors* theme = fl_theme_colors();
    FlLayoutId id = header_id(self, LIST_ID_HEADER_HANDLE(index));
    fl_layout_start(id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(LIST_HEADER_HANDLE_WIDTH), sizing_grow() },
        .child_alignment = { .x = LayoutAlignmentX_Right },
        .clip = { .horizontal = true },
    });
    FlSignal signal = fl_item_behavior(id, FlItemFlags_clickable | FlItemFlags_draggable);
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(1.0f), sizing_grow() },
        .color = signal.hovered ? theme->interactive.hover : theme->border.separator,
    });
    fl_layout_end();
    fl_layout_end();
    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static f32 cell_width_or_min(f32 width) {
    return width > LIST_CELL_MIN_WIDTH ? width : LIST_CELL_MIN_WIDTH;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The title in a box of a fixed width, so a long title is elided and leaves the handle its place.
static void header_title(FlString title, f32 width) {
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(cell_width_or_min(width)), sizing_grow() },
        .padding = { LIST_HEADER_PAD_X, 0, 0, 0 },
        .child_alignment = { .y = LayoutAlignmentY_Center },
    });
    ui_text_styled(title, .color = fl_theme_colors()->text.secondary, .no_wrap = true, .overflow = FlOverflow_Elide);
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void header_cell(FlListContext* self, ListState* st, u32 index) {
    const FlColumnDef* col = &self->columns[index];
    FlLayoutId id = header_id(self, LIST_ID_HEADER_CELL(index));
    fl_layout_start(id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(self->column_widths[index]), sizing_grow() },
        .child_alignment = { .y = LayoutAlignmentY_Center },
        .clip = { .horizontal = true },
    });
    FlSignal cell = fl_item_behavior(id, FlItemFlags_clickable);
    if (cell.pressed) {
        self->frame_header_pressed = true;
    }
    if (cell.clicked) {
        self->header_clicked_column = col->id;
    }

    header_title(col->title, self->column_widths[index] - LIST_HEADER_HANDLE_WIDTH);
    header_resize(self, st, index, header_handle(self, index));
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void header_emit(FlListContext* self, ListState* st, f32 height) {
    fl_layout_start(header_id(self, LIST_ID_HEADER));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fixed(height) },
        .color = fl_theme_colors()->surface.elevated,
        .clip = { .horizontal = true },
    });
    for_count(i, self->column_count) {
        if (self->column_widths[i] > 0.0f) {
            header_cell(self, st, i);
        }
    }
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void validate_column_ids(const FlColumnDef* columns, u32 count) {
    for_count(i, count) {
        FL_ASSERT_FATAL_MSG(columns[i].id != 0, "ListConfig.columns: a column id is 0");
        for_count(j, i) {
            FL_ASSERT_FATAL_MSG(columns[j].id != columns[i].id, "ListConfig.columns: two columns share an id");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_header_begin(FlListContext* self, ListState* st, const FlListConfig* config, f32 row_height) {
    self->columns = config->columns;
    self->column_count = config->column_count;
    self->column_widths = nullptr;
    self->result_width_count = 0;
    if (self->column_count == 0) {
        return;
    }
    validate_column_ids(self->columns, self->column_count);

    self->column_widths = arena_alloc_array(self->ui_ctx->render_frame_allocator, f32, self->column_count);
    f32 scale = ui_scale_or_one(self->ui_ctx->ui_scale);
    f32 available = list_uniform_content_width(self->ui_ctx->list_uniform_ctx, self->current_id.name) / scale;
    self->result_width_count = available > 0.0f ? self->column_count : 0;
    list_resolve_column_widths(self->columns, self->column_count, available, self->column_widths);

    fl_layout_start(header_id(self, LIST_ID_WRAPPER));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .direction = LayoutDirection_TopToBottom,
    });
    header_emit(self, st, config->header_height > 0.0f ? config->header_height : row_height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_header_end(FlListContext* self) {
    if (self->column_count > 0) {
        fl_layout_end();
    }
    self->columns = nullptr;
    self->column_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A cell is two boxes: the outer one cuts off what cannot shrink, and the inner one, which does not clip, lets the
// layout shrink what can (elided text, a growing child) to the column width.
void fl_cell_begin(u32 column_id) {
    FlListContext* self = g_ui_ctx->ui_list_ctx;
    u32 index = 0;
    f32 left = 0.0f;
    while (index < self->column_count && self->columns[index].id != column_id) {
        left += self->column_widths[index];
        index++;
    }
    FL_ASSERT_FATAL_MSG(index < self->column_count, "Cell::begin: column id is not in ListConfig.columns");
    FL_ASSERT(index >= self->cell_next_index && "Cell::begin: cells are drawn in column order");
    self->cell_next_index = index + 1;

    f32 right = left + self->column_widths[index];
    if (self->cell_cursor < left) {
        ui_spacer_h(left - self->cell_cursor);
        self->cell_cursor = left;
    }
    f32 width = right - self->cell_cursor;
    if (right > self->cell_cursor) {
        self->cell_cursor = right;
    }

    width = cell_width_or_min(width);
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(width), sizing_fixed(self->geometry.row_height) },
        .clip = { .horizontal = true },
    });
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(width), sizing_grow() },
        .padding = { LIST_HEADER_PAD_X, LIST_HEADER_PAD_X, 0, 0 },
        .child_alignment = { .y = LayoutAlignmentY_Center },
    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_cell_end(void) {
    fl_layout_end();
    fl_layout_end();
}
