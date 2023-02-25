#include <assert.h>
#include "internal.h"
#include "layout_private.h"

#if 0

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlLayoutAreaId fl_layout_area_create_impl(struct FlContext* self, FlString name, FlLayoutArea area) {
    const int id = self->layout_ids++;
    LayoutAreaPrivate* layout = LinearAllocator_alloc_zero(&self->layout_allocator, LayoutAreaPrivate);
    layout->area = area;
    layout->area.name = name;
    layout->id = id;
    return (FlLayoutAreaId)id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_set(struct FlContext* self, FlLayoutAreaId area) {
    self->current_layout = ((LayoutAreaPrivate*)self->layout_allocator.start_data) + area;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_area_set_layout_mode_impl(struct FlContext* self, FlLayoutMode mode) {
    self->layout_mode = mode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Layout_create_default(struct FlContext* self) {
    FlLayoutArea layout = {
        .width = {.value = 0, .value_type = FlSizeType_Stretch},
        .height = {.value = 0, .value_type = FlSizeType_Stretch},
        .direction = FlLayoutDirection_Verticial,
    };

    self->default_layout = fl_layout_area_create(self, "flowi_default_layout", layout);
    fl_layout_set(self, self->default_layout);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Layout_resolve(struct FlContext* self, FlLayoutAreaId id, FlRect* rect) {
    // TODO: Need to calculate children
    LayoutAreaPrivate* area = ((LayoutAreaPrivate*)self->layout_allocator.start_data) + id;

    // int width = rect->x1 - rect->x0;
    // int height = rect->y1 - rect->y0;

    // TODO:
    /*
    if (area->area.width.value_type == FlSizeType_Stretch) {
        area->total_size.y1 = rect->y1;
    } else if (area->area.width.value_type == FlSizeType_Fixed) {
        area->total_size.y1 = rect->y0 + area->area.height.value;
    }

    if (area->area.height.value_type == FlSizeType_Stretch) {
        area->total_size.x1 = rect->x1;
    } else if (area->area.width.value_type == FlSizeType_Fixed) {
        area->total_size.x1 = rect->x0 + area->area.width.value;
    }
    */

    area->total_size.x = rect->x;
    area->total_size.y = rect->y;
    area->updating_size = area->total_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Layout_add_item(struct FlContext* self, FlLayoutAreaId id, FlRect* out, int width, int height) {
    FL_UNUSED(width);

    LayoutAreaPrivate* area = ((LayoutAreaPrivate*)self->layout_allocator.start_data) + id;
    // TODO: Fix hard-coded layout direction

    area->updating_size.y += height;
    *out = area->updating_size;

    if ((area->updating_size.y + area->updating_size.height) >= (area->total_size.y + area->total_size.height)) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void fl_layout_update_area(FlLayoutAreaId area, FlLayoutArea* area) {
    LayoutAreaPrivate* a = ((LayoutAreaPrivate*)self->layout_allocator.start_data) + area;
    a->area = *area;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_set_arrangement_ids(FlLayoutAreaId area, FlLayoutAreaId* areas, int rows, int cols) {
    LayoutAreaPrivate* a = ((LayoutAreaPrivate*)self->layout_allocator.start_data) + area;
    assert((rows * cols) < 128); // TODO: fixme
    memcpy(a->layout, areas, rows * cols * sizeof(FlLayoutAreaId));
    a->rows = rows;
    a->cols = cols;
}

// Updates the layout
void Layout_update(LayoutAreaPrivate* layout, FlLayoutRect rect) {
    // if we have no children assume that we just maximize the area
    if (layout->rows == 0 && layout->cols == 0) {
        layout->total_size = rect;
        layout->updating_size = rect;
    } else {
        // TODO: Handle if we have layouts
    }
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void fl_layout_debug_render_current(struct FlContext* self) {



}
*/

#endif
