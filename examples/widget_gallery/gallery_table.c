#include "gallery_table.h"

#include <stdio.h>
#include <string.h>

#include <core/core.h>
#include <flowi/core/utils.h>
#include <flowi/ui/cell.h>
#include <flowi/ui/frame.h>
#include <flowi/ui/theme.h>
#include "ui/layout.h"
#include "ui/ui.h"
#include "ui/list/ui_list.h"
#include "ui/ui_selection.h"

#define GALLERY_TABLE_HEIGHT 200.0f
#define GALLERY_TABLE_ROW_HEIGHT 26.0f
#define GALLERY_TABLE_GROUP_EVERY 8u

enum { TableColumn_Module = 1, TableColumn_Base, TableColumn_Path };

static const char* const s_module_names[] = { "libflowi.so", "libc.so.6", "libSDL3.so.0", "debugger", "libm.so.6" };

void gallery_columns_apply(GalleryColumns* columns, const FlListResult* result) {
    if (result->header_clicked_column != 0) {
        columns->header_clicked = result->header_clicked_column;
    }
    if (result->context_menu_cell != 0) {
        columns->menu = result->context_menu_cell;
    }
    for_count(i, GALLERY_TABLE_COLUMNS) {
        if (columns->columns[i].id == result->resized_column) {
            columns->columns[i].width = result->resized_width;
        }
    }
}

void gallery_columns_status(const GalleryColumns* columns) {
    char buf[96];
    snprintf(buf, sizeof(buf), "column under mouse %u, header clicked %u, right-clicked %u", columns->hovered,
             columns->header_clicked, columns->menu);
    ui_text(fl_frame_str(buf, strlen(buf)));
}

void gallery_cell_text(u32 column_id, FlString text) {
    fl_cell_begin(column_id);
    ui_text_styled(text, .color = fl_theme_colors()->text.primary, .no_wrap = true, .overflow = FlOverflow_Elide);
    fl_cell_end();
}

static void cell_format(u32 column_id, const char* format, u32 a, const char* b) {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), format, a, b);
    gallery_cell_text(column_id, fl_frame_str(buf, (size_t)n));
}

static bool is_group_row(u32 row) {
    return row % GALLERY_TABLE_GROUP_EVERY == 0;
}

// A row that draws no cell spans every column.
static void group_row(u32 row) {
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "Process %u", row / GALLERY_TABLE_GROUP_EVERY + 1);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(GALLERY_TABLE_ROW_HEIGHT) },
                   .padding = { 6, 6, 0, 0 }, .child_alignment = { .y = LayoutAlignmentY_Center }) {
        ui_text_styled(fl_frame_str(buf, (size_t)n), .color = fl_theme_colors()->text.secondary);
    }
}

static void module_row(u32 row) {
    const char* name = s_module_names[row % sizeof_array(s_module_names)];
    gallery_cell_text(TableColumn_Module, fl_frame_str(name, strlen(name)));
    cell_format(TableColumn_Base, "0x%08x%s", 0x7f000000u + row * 0x1000u, "");
    cell_format(TableColumn_Path, "/usr/lib/x86_64-linux-gnu/build-%u/%s", row, name);
}

static void table_row(GalleryTable* table, u32 row) {
    FlSignal signal = fl_list_item_begin(table->ids[row]);
    if (signal.cell != 0) {
        table->columns.hovered = signal.cell;
    }
    if (is_group_row(row)) {
        group_row(row);
    } else {
        module_row(row);
    }
    fl_list_item_end();
}

void gallery_table_init(GalleryTable* table) {
    table->selection = selection_set_create(16);
    for_count(i, GALLERY_TABLE_ROWS) {
        table->ids[i] = (u64)i + 1;
    }
    table->columns.columns[0] = (FlColumnDef) { .id = TableColumn_Module, .title = FL_S_("Module"), .width = 160.0f };
    table->columns.columns[1] = (FlColumnDef) { .id = TableColumn_Base, .title = FL_S_("Base"), .width = 120.0f };
    table->columns.columns[2]
        = (FlColumnDef) { .id = TableColumn_Path, .title = FL_S_("Path"), .flex = 1.0f, .min_width = 80.0f };
}

void gallery_table_destroy(GalleryTable* table) {
    selection_set_destroy(table->selection);
}

void gallery_table_show(GalleryTable* table) {
    table->columns.hovered = 0;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(GALLERY_TABLE_HEIGHT) },
                   .direction = LayoutDirection_TopToBottom) {
        FlListRange range
            = fl_list_begin(ID("gallery_table"), table->ids, GALLERY_TABLE_ROWS, table->selection,
                            .row_height = GALLERY_TABLE_ROW_HEIGHT, .gap = 2.0f, .columns = table->columns.columns,
                            .column_count = GALLERY_TABLE_COLUMNS, .scrollbar_mode = FlScrollbarDisplayMode_Auto);
        for (u32 row = range.start; row < range.end; row++) {
            table_row(table, row);
        }
        FlListResult result = fl_list_end();
        gallery_columns_apply(&table->columns, &result);
    }
    gallery_columns_status(&table->columns);
}
