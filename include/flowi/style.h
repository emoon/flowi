
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"
#include "math_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlStyleColor {
    FlStyleColor_Text = 0,
    FlStyleColor_TextDisabled = 1,
    FlStyleColor_WindowBg = 2,
    FlStyleColor_ChildBg = 3,
    FlStyleColor_PopupBg = 4,
    FlStyleColor_Border = 5,
    FlStyleColor_BorderShadow = 6,
    FlStyleColor_FrameBg = 7,
    FlStyleColor_FrameBgHovered = 8,
    FlStyleColor_FrameBgActive = 9,
    FlStyleColor_TitleBg = 10,
    FlStyleColor_TitleBgActive = 11,
    FlStyleColor_TitleBgCollapsed = 12,
    FlStyleColor_MenuBarBg = 13,
    FlStyleColor_ScrollbarBg = 14,
    FlStyleColor_ScrollbarGrab = 15,
    FlStyleColor_ScrollbarGrabHovered = 16,
    FlStyleColor_ScrollbarGrabActive = 17,
    FlStyleColor_CheckMark = 18,
    FlStyleColor_SliderGrab = 19,
    FlStyleColor_SliderGrabActive = 20,
    FlStyleColor_Button = 21,
    FlStyleColor_ButtonHovered = 22,
    FlStyleColor_ButtonActive = 23,
    FlStyleColor_Header = 24,
    FlStyleColor_HeaderHovered = 25,
    FlStyleColor_HeaderActive = 26,
    FlStyleColor_Separator = 27,
    FlStyleColor_SeparatorHovered = 28,
    FlStyleColor_SeparatorActive = 29,
    FlStyleColor_ResizeGrip = 30,
    FlStyleColor_ResizeGripHovered = 31,
    FlStyleColor_ResizeGripActive = 32,
    FlStyleColor_Tab = 33,
    FlStyleColor_TabHovered = 34,
    FlStyleColor_TabActive = 35,
    FlStyleColor_TabUnfocused = 36,
    FlStyleColor_TabUnfocusedActive = 37,
    FlStyleColor_DockingPreview = 38,
    FlStyleColor_DockingEmptyBg = 39,
    FlStyleColor_PlotLines = 40,
    FlStyleColor_PlotLinesHovered = 41,
    FlStyleColor_PlotHistogram = 42,
    FlStyleColor_PlotHistogramHovered = 43,
    FlStyleColor_TableHeaderBg = 44,
    FlStyleColor_TableBorderStrong = 45,
    FlStyleColor_TableBorderLight = 46,
    FlStyleColor_TableRowBg = 47,
    FlStyleColor_TableRowBgAlt = 48,
    FlStyleColor_TextSelectedBg = 49,
    FlStyleColor_DragDropTarget = 50,
    FlStyleColor_NavHighlight = 51,
    FlStyleColor_NavWindowingHighlight = 52,
    FlStyleColor_NavWindowingDimBg = 52,
    FlStyleColor_ModalWindowDimBg = 53,
} FlStyleColor;

typedef enum FlStyleSingle {
    FlStyleSingle_Alpha = 0,
    FlStyleSingle_DisabledAlpha = 1,
    FlStyleSingle_WindowRounding = 2,
    FlStyleSingle_WindowBorderSize = 3,
    FlStyleSingle_ChildRounding = 4,
    FlStyleSingle_ChildBorderSize = 5,
    FlStyleSingle_PopupRounding = 6,
    FlStyleSingle_PopupBorderSize = 7,
    FlStyleSingle_FrameRounding = 8,
    FlStyleSingle_FrameBorderSize = 9,
    FlStyleSingle_IndentSpacing = 10,
    FlStyleSingle_ScrollbarSize = 11,
    FlStyleSingle_ScrollbarRounding = 12,
    FlStyleSingle_GrabMinSize = 13,
    FlStyleSingle_GrabRounding = 14,
    FlStyleSingle_TabRounding = 15,
} FlStyleSingle;

typedef enum FlStyleVec2 {
    FlStyleVec2_WindowPadding = 0,
    FlStyleVec2_WindowMinSize = 1,
    FlStyleVec2_WindowTitleAlign = 2,
    FlStyleVec2_FramePadding = 3,
    FlStyleVec2_ItemSpacing = 4,
    FlStyleVec2_ItemInnerSpacing = 5,
    FlStyleVec2_CellPadding = 6,
    FlStyleVec2_ButtonTextAlign = 7,
    FlStyleVec2_SelectableTextAlign = 8,
} FlStyleVec2;

struct FlStyle;
// Permantly set a color
static void fl_style_set_color(struct FlContext* ctx, FlStyleColor color, FlColor value);

// Permantly set a color (ARGB)
static void fl_style_set_color_u32(struct FlContext* ctx, FlStyleColor color, uint32_t value);

// Temporary push a color change (ARGB)
static void fl_style_push_color_u32(struct FlContext* ctx, FlStyleColor color, uint32_t value);

// Temporary push a color change
static void fl_style_push_color(struct FlContext* ctx, FlStyleColor color, FlColor value);

// Temporary push a color change
static void fl_style_pop_color(struct FlContext* ctx);

// Pushes a single style change
static void fl_style_push_single(struct FlContext* ctx, FlStyleSingle style, float value);

// Pushes a Vec2 style change
static void fl_style_push_vec2(struct FlContext* ctx, FlStyleVec2 style, FlVec2 value);

// Pops a style change
static void fl_style_pop(struct FlContext* ctx);

#include "style.inl"

#ifdef __cplusplus
}
#endif