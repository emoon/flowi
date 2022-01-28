
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "idx.h"
#include "manual.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlLayoutDirection {
    FlLayoutDirection_Horizontal = 0,
    FlLayoutDirection_Directional = 1,
} FlLayoutDirection;

typedef enum FlSizeType {
    FlSizeType_Fixed = 0,
    FlSizeType_Stretch = 1,
} FlSizeType;

typedef struct FlLayoutRect {
    int x0;
    int y0;
    int x1;
    int y1;
} FlLayoutRect;

typedef struct FlSizing {
    int value;
    FlSizeType value_type;
} FlSizing;

typedef struct FlLayoutArea {
    FlString name;
    FlSizing width;
    FlSizing height;
} FlLayoutArea;

FlLayoutArea fl_layout_area_new_impl(struct FlContext* ctx);

FL_INLINE FlLayoutArea fl_layout_area_new() {
    extern struct FlContext* g_fl_ctx;
    return fl_layout_area_new_impl(g_fl_ctx);
}

void fl_layout_area_from_children_impl(struct FlContext* ctx, FlLayoutArea children, int16_t row, int16_t cols);

FL_INLINE void fl_layout_area_from_children(FlLayoutArea children, int16_t row, int16_t cols) {
    extern struct FlContext* g_fl_ctx;
    fl_layout_area_from_children_impl(g_fl_ctx, children, row, cols);
}

#ifdef __cplusplus
}
#endif
