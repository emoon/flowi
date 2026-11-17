#pragma once

#include <core/types.h>
#include <flowi/core/string.h>
#include <flowi/ui/list.h>
#include <flowi/ui/ui_selection.h>

#define GALLERY_TABLE_COLUMNS 3
#define GALLERY_TABLE_ROWS 5000u

// The widths live here: a resize reported by the list or tree is written back into `columns`.
typedef struct GalleryColumns {
    FlColumnDef columns[GALLERY_TABLE_COLUMNS];
    u32 hovered;
    u32 header_clicked;
    u32 menu;
} GalleryColumns;

typedef struct GalleryTable {
    FlSelectionSet* selection;
    u64 ids[GALLERY_TABLE_ROWS];
    GalleryColumns columns;
} GalleryTable;

void gallery_columns_apply(GalleryColumns* columns, const FlListResult* result);
void gallery_columns_status(const GalleryColumns* columns);
void gallery_cell_text(u32 column_id, FlString text);

void gallery_table_init(GalleryTable* table);
void gallery_table_destroy(GalleryTable* table);
void gallery_table_show(GalleryTable* table);
