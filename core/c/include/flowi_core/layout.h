
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "context.h"
#include "idx.h"
#include "manual.h"
#include "math_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlLayoutDirection {
    FlLayoutDirection_Horizontal = 0,
    FlLayoutDirection_Verticial = 1,
} FlLayoutDirection;

typedef enum FlSizeType {
    FlSizeType_Fixed = 0,
    FlSizeType_Stretch = 1,
} FlSizeType;

// LayoutMode make it possible to select how ui elements are being layed out.
typedef enum FlLayoutMode {
    // Automatic (default) will use [LayoutArea] to do automatic positining. See [LayoutArea] for more info on how to
    // use this.
    FlLayoutMode_Automatic = 0,
    // User will have to use the [Ui::set_position]
    FlLayoutMode_Manual = 1,
} FlLayoutMode;

typedef struct FlSizing {
    int value;
    FlSizeType value_type;
} FlSizing;

typedef uint64_t FlLayoutAreaId;

typedef struct FlLayoutArea {
    FlString name;
    FlSizing width;
    FlSizing height;
    FlLayoutDirection direction;
} FlLayoutArea;

FlLayoutAreaId fl_layout_area_create_impl(struct FlContext* ctx, FlString name, FlLayoutArea area);

FL_INLINE FlLayoutAreaId fl_layout_area_create(struct FlContext* ctx, const char* name, FlLayoutArea area) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_layout_area_create_impl(ctx, name_, area);
}

FlLayoutAreaId fl_layout_area_from_children_impl(struct FlContext* ctx, FlString name, FlLayoutArea* children,
                                                 uint32_t children_size, int16_t row, int16_t cols);

FL_INLINE FlLayoutAreaId fl_layout_area_from_children(struct FlContext* ctx, const char* name, FlLayoutArea* children,
                                                      uint32_t children_size, int16_t row, int16_t cols) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_layout_area_from_children_impl(ctx, name_, children, children_size, row, cols);
}

void fl_layout_area_set_layout_mode_impl(struct FlContext* ctx, FlLayoutMode mode);

FL_INLINE void fl_layout_area_set_layout_mode(struct FlContext* ctx, FlLayoutMode mode) {
    fl_layout_area_set_layout_mode_impl(ctx, mode);
}

#ifdef __cplusplus
}
#endif
