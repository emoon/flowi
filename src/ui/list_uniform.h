#pragma once

// Internal header for the uniform list.
//
// Sits behind the public fl_list_uniform_* API: the context struct and the receiver-taking
// free functions the public ctx-free bodies forward into, plus the scroll introspection/drive and per-frame
// update hooks that stay internal.

#include <core/string.h> // FlString; the generated header takes it from its includer
#include <core/types.h>
#include <flowi/ui/layout.h>
#include <flowi/ui/list_uniform.h>
#include "list_geometry.h"

typedef struct FlArena FlArena;
typedef struct FlListUniformContext FlListUniformContext;
typedef struct FlUiCtx FlUiCtx;

// arena backs internal state allocations.
FlListUniformContext* list_uniform_create(FlArena* arena);

void list_uniform_destroy(FlListUniformContext* self);

// The Row Geometry of a list with count rows at the context's UI scale. A row_height or gap of 0 or less takes the
// default.
ListRowGeometry list_uniform_geometry(const FlUiCtx* ctx, f32 row_height, f32 gap, u32 count);

// What list_uniform_begin_rows does about scrolling this frame.
typedef struct ListUniformScroll {
    u32 scroll_to; // Row to bring into view, as its index plus one; 0 leaves the scroll position alone
    FlScrollbarDisplayMode scrollbar_mode;
} ListUniformScroll;

// The container's rect in window-space physical pixels.
typedef struct ListViewport {
    f32 top;
    f32 left;
    f32 right;
    f32 bottom;
} ListViewport;

// Opens the scroll container of list id for rows. Emit the rows of the returned range with list_uniform_row_begin
// or list_uniform_item_begin, then call list_uniform_end.
FlListUniformRange list_uniform_begin_rows(FlListUniformContext* self, FlString id, ListRowGeometry rows,
                                           ListUniformScroll scroll);

// list_uniform_begin_rows with the Row Geometry made from opts.
FlListUniformRange list_uniform_begin(FlListUniformContext* self, FlString id, u32 total_count, FlListUniformOpts opts);

// Opens the element of row index and returns its layout id. decoration supplies everything but the sizing. Close it
// with fl_layout_end.
FlLayoutId list_uniform_row_begin(FlListUniformContext* self, u32 index, FlLayoutConfig decoration);

void list_uniform_item_begin(FlListUniformContext* self, u32 index);
void list_uniform_item_end(FlListUniformContext* self);
void list_uniform_end(FlListUniformContext* self);

// Scroll introspection / drive. Keyed by the container's string id so a caller may read/drive a list outside its
// begin->end scope. All no-op / return 0 or false for an unknown id.

// Current smoothed scroll offset (pixels from the top of the content) for list id.
f64 list_uniform_scroll_offset(FlListUniformContext* self, FlString id);

// Move list id's scroll offset and smooth-scroll target by delta_px, clamped to the rows the list was last begun
// with. Positive scrolls toward the end.
void list_uniform_nudge_scroll(FlListUniformContext* self, FlString id, f32 delta_px);

// The width, in physical pixels, that rows of list id got last frame: the container width minus the scrollbar
// reserve. 0 until the list has been laid out once, or for an unknown id.
f32 list_uniform_content_width(FlListUniformContext* self, FlString id);

// The container rect of list id from the previous frame's layout. False until the list has been laid out.
bool list_uniform_viewport(const FlListUniformContext* self, FlString id, ListViewport* out);

// The mouse is over the scrollbar of list id or dragging it, as of the list's last list_uniform_end.
bool list_uniform_scrollbar_engaged(FlListUniformContext* self, FlString id);

void list_uniform_begin_update(FlListUniformContext* self);
