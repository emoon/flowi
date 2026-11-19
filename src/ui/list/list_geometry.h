#pragma once

// Row Geometry (docs/CONTEXT.md "Lists"): where the rows of a uniform list are. The functions are pure: no
// context, no layout, no input. Content Y is in physical pixels from the top of the first row.

#include "../scroll_core.h"
#include <core/types.h>

typedef struct ListRowGeometry {
    f32 row_height; // Height of a row, logical pixels
    f32 gap;        // Space between two rows, logical pixels
    f32 row_px;     // Height of a row, physical pixels
    f32 gap_px;     // Space between two rows, whole physical pixels
    u32 count;      // Number of rows
} ListRowGeometry;

// Rows first to last, both included. Empty when first > last.
typedef struct ListRowSpan {
    i64 first;
    i64 last;
} ListRowSpan;

// A row_height or gap of 0 or less takes the default. A ui_scale of 0 or less counts as 1.
ListRowGeometry list_row_geometry(f32 row_height, f32 gap, f32 ui_scale, u32 count);

// A row and its gap take up more than zero pixels.
bool list_geometry_rows_have_extent(ListRowGeometry rows);

// Height of all rows, each with its gap.
f64 list_geometry_content_height(ListRowGeometry rows);

// Height of row_count adjacent rows with the gaps between them.
f32 list_geometry_span_height(ListRowGeometry rows, u32 row_count);

// Content Y of the top of row.
f64 list_geometry_row_top(ListRowGeometry rows, u32 row);

// Rows [start, end) that intersect a viewport scrolled to scroll_offset, padded by buffer rows on each side.
ScrollRange list_geometry_visible_rows(ListRowGeometry rows, f64 scroll_offset, f32 viewport, u32 buffer);

// Rows that intersect the content Y band [lo, hi]. A band that lies inside one gap covers no row.
ListRowSpan list_geometry_rows_in_band(ListRowGeometry rows, f64 lo, f64 hi);

// Insertion index in [0, count] for a drop at content_y: the index of the row under it when content_y is in that
// row's top half, and one more otherwise. 0 for a list with no rows.
u32 list_geometry_insertion_index(ListRowGeometry rows, f64 content_y);

// Content Y of the middle of the gap above the row at an insertion index.
f64 list_geometry_insertion_y(ListRowGeometry rows, u32 index);
