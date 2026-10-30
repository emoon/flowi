#pragma once

#include <core/math.h>
#include <core/string.h>
#include <core/types.h>
#include "color.h"
#include <flowi/core/platform.h>
#include <flowi/ui/floating.h> // @generated FlFloatingConfig / FlFloatingAttach* (api_gen)
#include <flowi/ui/layout.h>
#include <flowi/ui/ui_floating.h> // @generated fl_floating_* prototypes (api_gen)
#include <flowi/ui/ui_layout.h>   // @generated fl_layout_* prototypes (api_gen)
#include <ui/ui_types.h>
#include "ui_scroll.h"

#define corner_radius(radius) ((FlCornerRadius) { radius, radius, radius, radius })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define STRING_ENSURE_STRING_LITERAL_ID(x) ("" x "")
#define ID(str) ((FlLayoutId) { .name = { STRING_ENSURE_STRING_LITERAL_ID(str), sizeof(str) - 1, 1 } })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ID_INT(str, int_id) ((FlLayoutId) { .name = { str, sizeof(str) - 1, 1 }, .id = int_id })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define LayoutDirection_LeftToRight FlLayoutDirection_LeftToRight
#define LayoutDirection_TopToBottom FlLayoutDirection_TopToBottom

#define LayoutAlignmentX_Left FlLayoutAlignmentX_Left
#define LayoutAlignmentX_Right FlLayoutAlignmentX_Right
#define LayoutAlignmentX_Center FlLayoutAlignmentX_Center

#define LayoutAlignmentY_Top FlLayoutAlignmentY_Top
#define LayoutAlignmentY_Bottom FlLayoutAlignmentY_Bottom
#define LayoutAlignmentY_Center FlLayoutAlignmentY_Center

// Shorthand for the .child_alignment field: the x/y args are the alignment names without their enum prefix,
// as in .child_alignment = ui_align(Center, Center). x is Left/Right/Center, y is Top/Bottom/Center.
#define ui_align(x, y) { LayoutAlignmentX_##x, LayoutAlignmentY_##y }

#define LayoutSizingType_Fit FlLayoutSizingType_Fit
#define LayoutSizingType_Grow FlLayoutSizingType_Grow
#define LayoutSizingType_Percent FlLayoutSizingType_Percent
#define LayoutSizingType_Fixed FlLayoutSizingType_Fixed

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Usage: .border = fl_border(theme->border.focus, 2)  // Creates uniform 2px border

#define fl_border(col, width_value)             \
    (UI__INIT(FlLayoutBorder) { .color = (col), \
                                .width = { (width_value), (width_value), (width_value), (width_value), 0 } })

#define fl_border_lrtb(col, left, right, top, bottom) \
    (UI__INIT(FlLayoutBorder) { .color = (col), .width = { (left), (right), (top), (bottom), 0 } })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define padding_all(padding) (UI__INIT(FlLayoutPadding) { padding, padding, padding, padding })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define sizing_fit(...) \
    (UI__INIT(FlLayoutSizingAxis) { .size = { .min_max = { __VA_ARGS__ } }, .type = FlLayoutSizingType_Fit })

#define sizing_grow(...) \
    (UI__INIT(FlLayoutSizingAxis) { .size = { .min_max = { __VA_ARGS__ } }, .type = FlLayoutSizingType_Grow })

#define sizing_fixed(fixed_size)                                                       \
    (UI__INIT(FlLayoutSizingAxis) { .size = { .min_max = { fixed_size, fixed_size } }, \
                                    .type = FlLayoutSizingType_Fixed })

#define sizing_percent(percent_of_parent) \
    (UI__INIT(FlLayoutSizingAxis) { .size = { .percent = (percent_of_parent) }, .type = FlLayoutSizingType_Percent })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop control variable of the UI_LAYOUT scope macros

static int UI_LAYOUT_LATCH UNUSED_VARIABLE = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The layout open/close/configure primitives are declared by the generated <flowi/ui/ui_layout.h> above.
// Floating comes from <flowi/ui/ui_floating.h>; scroll from ui_scroll.h.

FlVec2 ui_get_scroll_offset(void);

// Viewport size + content extent of a scroll container, keyed by its clay element id. The one sanctioned
// wrapper over Clay_GetScrollContainerData - content_extent is the piece ui_get_element_state does not expose
// (it carries only the element rect). found=false when the container is not in the previous frame's layout.
typedef struct FlScrollContainerExtent {
    FlVec2 viewport_size;
    FlVec2 content_extent;
    bool found;
} FlScrollContainerExtent;

FlScrollContainerExtent ui_get_scroll_container_extent(u32 element_id);

#define UI__WRAPPER_TYPE(type) UI__##type##Wrapper
#define UI__WRAPPER_STRUCT(type) \
    typedef struct {             \
        type wrapped;            \
    } UI__WRAPPER_TYPE(type)
#define UI__CONFIG_WRAPPER(type, ...) (UI__INIT(UI__WRAPPER_TYPE(type)) { { __VA_ARGS__ } }).wrapped

#define UI__INIT(type) (type)

UI__WRAPPER_STRUCT(FlLayoutConfig);
UI__WRAPPER_STRUCT(FlLayoutPadding);
UI__WRAPPER_STRUCT(FlFloatingConfig);
UI__WRAPPER_STRUCT(FlScrollAreaConfig);

// clang-format off
// Layout area with explicit ID - use when you need to track the element (e.g., for interaction, bounding box queries)
#define ui_layout_area_id(id, ...)  \
    for ( \
         UI_LAYOUT_LATCH = (fl_layout_start(id), fl_layout_configure_impl(UI__CONFIG_WRAPPER(FlLayoutConfig, __VA_ARGS__)), 0); \
         UI_LAYOUT_LATCH < 1; \
         UI_LAYOUT_LATCH = 1, fl_layout_end())

// Layout area without ID - use for pure layout containers that don't need tracking
#define ui_layout_area(...)  \
    for ( \
         UI_LAYOUT_LATCH = (fl_layout_start_auto(), fl_layout_configure_impl(UI__CONFIG_WRAPPER(FlLayoutConfig, __VA_ARGS__)), 0); \
         UI_LAYOUT_LATCH < 1; \
         UI_LAYOUT_LATCH = 1, fl_layout_end())

// Layout area with pre-built config struct - use with inline functions that return FlLayoutConfig
#define ui_layout_area_cfg(cfg)  \
    for ( \
         UI_LAYOUT_LATCH = (fl_layout_start_auto(), fl_layout_configure_impl(cfg), 0); \
         UI_LAYOUT_LATCH < 1; \
         UI_LAYOUT_LATCH = 1, fl_layout_end())

// Floating area with explicit ID - for dialogs/popups that need a specific ID
#define ui_floating_area_id(id, ...)  \
    for ( \
         UI_LAYOUT_LATCH = (fl_floating_start(id), fl_floating_configure_impl(UI__CONFIG_WRAPPER(FlFloatingConfig, __VA_ARGS__)), 0); \
         UI_LAYOUT_LATCH < 1; \
         UI_LAYOUT_LATCH = 1, fl_layout_end())

// Floating area without explicit ID - uses __LINE__ for unique ID
// Note: Clay's auto-ID doesn't work for floating elements (they don't increment parent's child count)
#define ui_floating_area(...)  \
    ui_floating_area_id(ID_INT("_Floating", __LINE__), __VA_ARGS__)

// Scroll area with explicit ID - use when you need to reference the scroll area programmatically
#define ui_scroll_area_id(id, ...)  \
    for ( \
         UI_LAYOUT_LATCH = (fl_scroll_area_begin_impl(id, UI__CONFIG_WRAPPER(FlScrollAreaConfig, __VA_ARGS__)), 0); \
         UI_LAYOUT_LATCH < 1; \
         UI_LAYOUT_LATCH = 1, fl_scroll_area_end())

// Scroll area without explicit ID - uses __LINE__ for stable unique ID
// Scroll state persists across frames as long as code structure doesn't change
#define ui_scroll_area(...)  \
    ui_scroll_area_id(ID_INT("_Scroll", __LINE__), __VA_ARGS__)
// clang-format on
