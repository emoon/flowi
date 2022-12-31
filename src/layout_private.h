#pragma once

#if 0

#include <flowi_core/layout.h>
#include <flowi_core/math_data.h>

struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LayoutAreaPrivate {
    FlLayoutArea area;
    int id;
    int frame;
    // TODO: Fixme
    FlLayoutAreaId layout[128];
    int rows;
    int cols;
    FlRect total_size;
    FlRect updating_size;
} LayoutAreaPrivate;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Layout_create_default(struct FlContext* self);
void Layout_resolve(struct FlContext* self, FlLayoutAreaId id, FlRect* rect);
bool Layout_add_item(struct FlContext* self, FlLayoutAreaId id, FlRect* out, int width, int height);

#endif
