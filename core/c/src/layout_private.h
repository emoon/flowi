#pragma once

#include "../include/layout.h"

typedef uint32_t FlLayoutAreaId;
struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LayoutAreaPrivate {
    FlLayoutArea area;
    int id;
    // TODO: Fixme
    FlLayoutAreaId layout[128];
    int rows;
    int cols;
    FlLayoutRect total_size;
    FlLayoutRect updating_size;
} LayoutAreaPrivate;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlLayoutAreaId fl_layout_area_create(FlContext* self, FlLayoutArea* area);

void fl_layout_set(struct FlContext* self, FlLayoutAreaId area);

//void fl_layout_update_area(FlLayoutAreaId area, FlLayoutArea* area);
//void fl_layout_set_arrangement_text(FlLayoutAreaId area, FlString string);
void fl_layout_set_arrangement_ids(FlLayoutAreaId area, FlLayoutAreaId* areas, int rows, int cols);
void fl_layout_area_add_child(FlLayoutAreaId area, FlLayoutAreaId child);

void fl_layout_debug_render_current(struct FlContext* self);

