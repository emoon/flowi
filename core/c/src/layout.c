#include "layout_private.h"
#include "internal.h"
#include <assert.h>


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlLayoutAreaId fl_layout_area_create(FlContext* self, FlLayoutArea* area) {
	const int id = self->layout_ids++;
	LayoutAreaPrivate* layout = LinearAllocator_alloc_zero(&self->layout_allocator, LayoutAreaPrivate);
	FL_TRY_ALLOC_INT(layout);
	layout->area = *area;
	layout->id = id;
	return (FlLayoutAreaId)id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_set(struct FlContext* self, FlLayoutAreaId area) {
	self->current_layout = ((LayoutAreaPrivate*)self->layout_allocator.start_data) + area;
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


