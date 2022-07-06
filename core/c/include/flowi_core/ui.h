
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "context.h"
#include "idx.h"
#include "image.h"
#include "layout.h"
#include "manual.h"
#include "math_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlLayerType {
    FlLayerType_Layer0 = 0,
    FlLayerType_Layer1 = 1,
    FlLayerType_Popup = 2,
    FlLayerType_Count = 3,
} FlLayerType;

typedef struct FlUi {
    uint32_t dummy;
} FlUi;

// Set the active layer for rendering
void fl_ui_set_layer_impl(struct FlContext* ctx, FlLayerType layer);

FL_INLINE void fl_ui_set_layer(struct FlContext* ctx, FlLayerType layer) {
    fl_ui_set_layer_impl(ctx, layer);
}

// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
void fl_ui_text_impl(struct FlContext* ctx, FlString text);

FL_INLINE void fl_ui_text(struct FlContext* ctx, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    fl_ui_text_impl(ctx, text_);
}

// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
void fl_ui_image_impl(struct FlContext* ctx, FlImage image);

FL_INLINE void fl_ui_image(struct FlContext* ctx, FlImage image) {
    fl_ui_image_impl(ctx, image);
}

// Draw image with given size
void fl_ui_image_with_size_impl(struct FlContext* ctx, FlImage image, FlVec2 size);

FL_INLINE void fl_ui_image_with_size(struct FlContext* ctx, FlImage image, FlVec2 size) {
    fl_ui_image_with_size_impl(ctx, image, size);
}

// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos);

FL_INLINE void fl_ui_set_pos(struct FlContext* ctx, FlVec2 pos) {
    fl_ui_set_pos_impl(ctx, pos);
}

// Get the last widget size. This is usually used for doing manual layouting
FlRect fl_ui_get_last_widget_size_impl(struct FlContext* ctx, FlVec2 pos);

FL_INLINE FlRect fl_ui_get_last_widget_size(struct FlContext* ctx, FlVec2 pos) {
    return fl_ui_get_last_widget_size_impl(ctx, pos);
}

// Push button widget that returns true if user has pressed it
bool fl_ui_push_button_with_icon_impl(struct FlContext* ctx, FlString text, FlImage image, FlVec2 text_pos,
                                      float image_scale);

FL_INLINE bool fl_ui_push_button_with_icon(struct FlContext* ctx, const char* text, FlImage image, FlVec2 text_pos,
                                           float image_scale) {
    FlString text_ = fl_cstr_to_flstring(text);
    return fl_ui_push_button_with_icon_impl(ctx, text_, image, text_pos, image_scale);
}

// Push button widget that returns true if user has pressed it
bool fl_ui_push_button_impl(struct FlContext* ctx, FlString text);

FL_INLINE bool fl_ui_push_button(struct FlContext* ctx, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    return fl_ui_push_button_impl(ctx, text_);
}

#ifdef __cplusplus
}
#endif
