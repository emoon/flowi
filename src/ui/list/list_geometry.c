#include "list_geometry.h"
#include "../pixel_snap.h"
#include <math.h>

#define LIST_ROW_DEFAULT_HEIGHT 32.0f // Row height when the caller leaves it unset (logical px)
#define LIST_ROW_DEFAULT_GAP 8.0f     // Gap between rows when the caller leaves it unset (logical px)

static f32 stride(ListRowGeometry rows) {
    return rows.row_px + rows.gap_px;
}

ListRowGeometry list_row_geometry(f32 row_height, f32 gap, f32 ui_scale, u32 count) {
    f32 scale = ui_scale_or_one(ui_scale);
    f32 height = row_height > 0.0f ? row_height : LIST_ROW_DEFAULT_HEIGHT;
    f32 row_gap = gap > 0.0f ? gap : LIST_ROW_DEFAULT_GAP;
    return (ListRowGeometry) {
        .row_height = height,
        .gap = row_gap,
        .row_px = height * scale,
        .gap_px = ui_scale_and_snap(row_gap, scale),
        .count = count,
    };
}

bool list_geometry_rows_have_extent(ListRowGeometry rows) {
    return stride(rows) > 0.0f;
}

f64 list_geometry_content_height(ListRowGeometry rows) {
    return (f64)rows.count * (f64)stride(rows);
}

f32 list_geometry_span_height(ListRowGeometry rows, u32 row_count) {
    if (row_count == 0) {
        return 0.0f;
    }
    return rows.row_px * (f32)row_count + rows.gap_px * (f32)(row_count - 1);
}

f64 list_geometry_row_top(ListRowGeometry rows, u32 row) {
    return (f64)row * (f64)stride(rows);
}

ScrollRange list_geometry_visible_rows(ListRowGeometry rows, f64 scroll_offset, f32 viewport, u32 buffer) {
    return scroll_visible_range(scroll_offset, viewport, stride(rows), rows.count, buffer);
}

ListRowSpan list_geometry_rows_in_band(ListRowGeometry rows, f64 lo, f64 hi) {
    f64 step = stride(rows);
    i64 count = (i64)rows.count;
    i64 first = (i64)ceil((lo - (f64)rows.row_px) / step);
    i64 last = (i64)ceil(hi / step) - 1;
    if (first < 0) {
        first = 0;
    }
    if (last > count - 1) {
        last = count - 1;
    }
    return (ListRowSpan) { first, last };
}

u32 list_geometry_insertion_index(ListRowGeometry rows, f64 content_y) {
    f64 step = stride(rows);
    if (rows.count == 0 || step <= 0.0) {
        return 0;
    }
    i64 row = (i64)floor(content_y / step);
    if (row < 0) {
        row = 0;
    }
    if (row > (i64)rows.count - 1) {
        row = (i64)rows.count - 1;
    }
    f64 row_mid = (f64)row * step + (f64)rows.row_px * 0.5;
    return content_y < row_mid ? (u32)row : (u32)row + 1;
}

f64 list_geometry_insertion_y(ListRowGeometry rows, u32 index) {
    return (f64)index * (f64)stride(rows) - (f64)rows.gap_px * 0.5;
}
