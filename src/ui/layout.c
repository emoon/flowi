#include "layout.h"
#include <core/assert.h>
#include <core/log.h>
#include "external/clay/clay.h"
#include "painter.h"
#include "pixel_snap.h"
#include "ui_private.h"
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Painting-scope bracketing. Every layout element open/close pushes/pops a painter scope in lockstep, capturing
// the just-opened element's CUSTOM customData slot so painter calls inside the scope attach there. The
// direct-Clay element opens elsewhere (popups, the text cursor) neither push nor pop - they are not painted into
// and keep the stack balanced with the public fl_layout_*/fl_floating_* opens, which always close via
// fl_layout_end.

static void ui_paint_scope_push(void) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx != nullptr && ctx->painter != nullptr) {
        painter_scope_push(ctx->painter, Clay_GetOpenElementCustomDataSlot());
    }
}

static void ui_paint_scope_pop(void) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx != nullptr && ctx->painter != nullptr) {
        painter_scope_pop(ctx->painter);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ui_apply_sizing(Clay_Sizing* out, FlLayoutSizing sizing, float scale) {
    if (sizing.width.type == FlLayoutSizingType_Percent) {
        out->width.size.percent = sizing.width.size.percent;
    } else {
        out->width.size.minMax.min = sizing.width.size.min_max.min * scale;
        out->width.size.minMax.max = sizing.width.size.min_max.max * scale;
    }
    out->width.type = (Clay__SizingType)sizing.width.type;

    if (sizing.height.type == FlLayoutSizingType_Percent) {
        out->height.size.percent = sizing.height.size.percent;
    } else {
        out->height.size.minMax.min = sizing.height.size.min_max.min * scale;
        out->height.size.minMax.max = sizing.height.size.min_max.max * scale;
    }
    out->height.type = (Clay__SizingType)sizing.height.type;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ui_open_element_with_id(FlLayoutId id) {
    // Open with the full Clay element id - including its stringId, so Clay's hashmap retains the name.
    Clay_ElementId element_id
        = Clay__HashString((Clay_String) { .chars = id.name.data, .length = (u32)id.name.length }, id.id);
#ifndef NDEBUG
    u32 element_id_value = element_id.id;
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx) {
        ItemState* existing = hashmap_get(&ctx->shared->debug_id_tracker, element_id_value);
        if (existing) {
            log_error("DUPLICATE ID DETECTED: name='%.*s' id=%u (hash=0x%08X)", (int)id.name.length, id.name.data,
                      id.id, element_id_value);
            FL_ASSERT_FATAL(!existing);
        } else {
            // Only presence matters, not the value
            ItemState dummy = { 0 };
            hashmap_insert(&ctx->shared->debug_id_tracker, element_id_value, dummy);
        }
    }
#endif

    Clay__OpenElementWithId(element_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_start(FlLayoutId id) {
    ui_open_element_with_id(id);
    ui_paint_scope_push();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_start_auto(void) {
    Clay__OpenElement();
    ui_paint_scope_push();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_floating_start(FlLayoutId id) {
    fl_layout_start(id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_floating_configure(FlFloatingConfig config) {
    Clay_ElementDeclaration decl = { 0 };

    FlUiCtx* ctx = ui_get_ctx();
    float scale = ctx ? ctx->ui_scale : 1.0f;
    ui_apply_sizing(&decl.layout.sizing, config.sizing, scale);

    switch (config.attach_to) {
        case FlFloatingAttachTo_None:
            decl.floating.attachTo = CLAY_ATTACH_TO_NONE;
            break;
        case FlFloatingAttachTo_Parent:
            decl.floating.attachTo = CLAY_ATTACH_TO_PARENT;
            break;
        case FlFloatingAttachTo_Element:
            decl.floating.attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID;
            decl.floating.parentId = config.parent_id;
            break;
        case FlFloatingAttachTo_Root:
            decl.floating.attachTo = CLAY_ATTACH_TO_ROOT;
            break;
    }

    decl.floating.pointerCaptureMode = (config.pointer_capture == FlFloatingPointerCapture_Passthrough)
                                           ? CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH
                                           : CLAY_POINTER_CAPTURE_MODE_CAPTURE;

    decl.floating.zIndex = config.z_index;
    decl.floating.offset.x = config.offset.x * scale;
    decl.floating.offset.y = config.offset.y * scale;

    // The cast is safe: the enums have the same order.
    decl.floating.attachPoints.element = (Clay_FloatingAttachPointType)config.element_attach;
    decl.floating.attachPoints.parent = (Clay_FloatingAttachPointType)config.parent_attach;

    Clay__ConfigureOpenElement(decl);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_end(void) {
    ui_paint_scope_pop();
    Clay__CloseElement();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 ui_get_scroll_offset(void) {
    Clay_Vector2 offset = Clay_GetScrollOffset();
    return (FlVec2) { .x = offset.x, .y = offset.y };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlScrollContainerExtent ui_get_scroll_container_extent(u32 element_id) {
    Clay_ScrollContainerData data = Clay_GetScrollContainerData((Clay_ElementId) { .id = element_id });
    if (!data.found) {
        return (FlScrollContainerExtent) { 0 };
    }
    return (FlScrollContainerExtent) {
        .viewport_size = { data.scrollContainerDimensions.width, data.scrollContainerDimensions.height },
        .content_extent = { data.contentDimensions.width, data.contentDimensions.height },
        .found = true,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_layout_configure(FlLayoutConfig config) {
    FlUiCtx* ctx = ui_get_ctx();
    float scale = ctx ? ctx->ui_scale : 1.0f;

    Clay_ElementDeclaration temp = { 0 };

    ui_apply_sizing(&temp.layout.sizing, config.sizing, scale);

    temp.layout.padding.left = (u16)ui_scale_and_snap(config.padding.left, scale);
    temp.layout.padding.right = (u16)ui_scale_and_snap(config.padding.right, scale);
    temp.layout.padding.top = (u16)ui_scale_and_snap(config.padding.top, scale);
    temp.layout.padding.bottom = (u16)ui_scale_and_snap(config.padding.bottom, scale);

    temp.layout.childGap = (u16)ui_scale_and_snap(config.child_gap, scale);

    temp.cornerRadius.topLeft = config.radius.tl * scale;
    temp.cornerRadius.topRight = config.radius.tr * scale;
    temp.cornerRadius.bottomLeft = config.radius.bl * scale;
    temp.cornerRadius.bottomRight = config.radius.br * scale;

    temp.layout.childAlignment = *(Clay_ChildAlignment*)&config.child_alignment;
    temp.layout.layoutDirection = (Clay_LayoutDirection)config.direction;
    temp.backgroundColor = (Clay_Color) { config.color.r, config.color.g, config.color.b, config.color.a };

    if (config.border.width.left > 0 || config.border.width.right > 0 || config.border.width.top > 0
        || config.border.width.bottom > 0 || config.border.width.between_children > 0) {
        temp.border.color = (Clay_Color) { config.border.color.r, config.border.color.g, config.border.color.b,
                                           config.border.color.a };
        temp.border.width.left = (u16)ui_scale_and_snap(config.border.width.left, scale);
        temp.border.width.right = (u16)ui_scale_and_snap(config.border.width.right, scale);
        temp.border.width.top = (u16)ui_scale_and_snap(config.border.width.top, scale);
        temp.border.width.bottom = (u16)ui_scale_and_snap(config.border.width.bottom, scale);
        temp.border.width.betweenChildren = (u16)ui_scale_and_snap(config.border.width.between_children, scale);
        // Dash lengths scale but do not snap.
        temp.border.dashLen = config.border.stroke.dash_len * scale;
        temp.border.gapLen = config.border.stroke.gap_len * scale;
    }

    temp.clip.horizontal = config.clip.horizontal;
    temp.clip.vertical = config.clip.vertical;

    // The caller sets manual_scroll when it owns scroll_pos; an offset of exactly zero is then still honored as
    // a manual offset. Scroll positions are in screen coordinates and do not scale.
    if (config.clip.manual_scroll) {
        temp.clip.childOffset.x = config.clip.scroll_pos.x;
        temp.clip.childOffset.y = config.clip.scroll_pos.y;
    } else if (config.clip.horizontal || config.clip.vertical) {
        // Clay-managed scrolling: the scroll position from the previous frame.
        Clay_Vector2 scroll_offset = Clay_GetScrollOffset();
        temp.clip.childOffset.x = scroll_offset.x;
        temp.clip.childOffset.y = scroll_offset.y;
    } else {
        temp.clip.childOffset.x = 0.0f;
        temp.clip.childOffset.y = 0.0f;
    }

    // Placed child (flowi divergence): when enabled, the engine positions this child at an explicit
    // content-space offset (scrolling and clipping with its parent) instead of by flex flow. The offset is
    // logical/reference pixels, so it scales like every other coordinate.
    temp.placed.enabled = config.placed.enabled;
    temp.placed.offset.x = config.placed.offset.x * scale;
    temp.placed.offset.y = config.placed.offset.y * scale;

    Clay__ConfigureOpenElement(temp);
}

// Static asserts pinning the Layout types to the Clay types they mirror.

_Static_assert(sizeof(FlLayoutDirection) == sizeof(Clay_LayoutDirection),
               "FlLayoutDirection size must match Clay_LayoutDirection");
_Static_assert(sizeof(FlLayoutAlignmentX) == sizeof(Clay_LayoutAlignmentX),
               "FlLayoutAlignmentX size must match Clay_LayoutAlignmentX");
_Static_assert(sizeof(FlLayoutAlignmentY) == sizeof(Clay_LayoutAlignmentY),
               "FlLayoutAlignmentY size must match Clay_LayoutAlignmentY");
_Static_assert(sizeof(FlLayoutSizingType) == sizeof(Clay__SizingType),
               "FlLayoutSizingType size must match Clay__SizingType");

_Static_assert((int)LayoutDirection_LeftToRight == (int)CLAY_LEFT_TO_RIGHT,
               "LayoutDirection_LeftToRight must match CLAY_LEFT_TO_RIGHT");
_Static_assert((int)LayoutDirection_TopToBottom == (int)CLAY_TOP_TO_BOTTOM,
               "LayoutDirection_TopToBottom must match CLAY_TOP_TO_BOTTOM");

_Static_assert((int)LayoutAlignmentX_Left == (int)CLAY_ALIGN_X_LEFT,
               "LayoutAlignmentX_Left must match CLAY_ALIGN_X_LEFT");
_Static_assert((int)LayoutAlignmentX_Right == (int)CLAY_ALIGN_X_RIGHT,
               "LayoutAlignmentX_Right must match CLAY_ALIGN_X_RIGHT");
_Static_assert((int)LayoutAlignmentX_Center == (int)CLAY_ALIGN_X_CENTER,
               "LayoutAlignmentX_Center must match CLAY_ALIGN_X_CENTER");

_Static_assert((int)LayoutAlignmentY_Top == (int)CLAY_ALIGN_Y_TOP, "LayoutAlignmentY_Top must match CLAY_ALIGN_Y_TOP");
_Static_assert((int)LayoutAlignmentY_Bottom == (int)CLAY_ALIGN_Y_BOTTOM,
               "LayoutAlignmentY_Bottom must match CLAY_ALIGN_Y_BOTTOM");
_Static_assert((int)LayoutAlignmentY_Center == (int)CLAY_ALIGN_Y_CENTER,
               "LayoutAlignmentY_Center must match CLAY_ALIGN_Y_CENTER");

_Static_assert((int)LayoutSizingType_Fit == (int)CLAY__SIZING_TYPE_FIT,
               "LayoutSizingType_Fit must match CLAY__SIZING_TYPE_FIT");
_Static_assert((int)LayoutSizingType_Grow == (int)CLAY__SIZING_TYPE_GROW,
               "LayoutSizingType_Grow must match CLAY__SIZING_TYPE_GROW");
_Static_assert((int)LayoutSizingType_Percent == (int)CLAY__SIZING_TYPE_PERCENT,
               "LayoutSizingType_Percent must match CLAY__SIZING_TYPE_PERCENT");
_Static_assert((int)LayoutSizingType_Fixed == (int)CLAY__SIZING_TYPE_FIXED,
               "LayoutSizingType_Fixed must match CLAY__SIZING_TYPE_FIXED");

_Static_assert(sizeof(FlLayoutChildAlignment) == sizeof(Clay_ChildAlignment),
               "FlLayoutChildAlignment size must match Clay_ChildAlignment");
_Static_assert(sizeof(FlLayoutSizingMinMax) == sizeof(Clay_SizingMinMax),
               "FlLayoutSizingMinMax size must match Clay_SizingMinMax");
_Static_assert(sizeof(FlLayoutSizingAxis) == sizeof(Clay_SizingAxis),
               "FlLayoutSizingAxis size must match Clay_SizingAxis");
_Static_assert(sizeof(FlLayoutSizing) == sizeof(Clay_Sizing), "FlLayoutSizing size must match Clay_Sizing");
_Static_assert(sizeof(FlLayoutPadding) == sizeof(Clay_Padding), "FlLayoutPadding size must match Clay_Padding");
_Static_assert(sizeof(FlCornerRadius) == sizeof(Clay_CornerRadius), "FlCornerRadius size must match Clay_CornerRadius");

// LayoutBorder has a different structure than Clay_BorderWidth, so only the basic members are verified.
_Static_assert(sizeof(uint16_t) == sizeof(((Clay_BorderWidth*)0)->left), "Border left member type must match");
_Static_assert(sizeof(uint16_t) == sizeof(((Clay_BorderWidth*)0)->right), "Border right member type must match");
_Static_assert(sizeof(uint16_t) == sizeof(((Clay_BorderWidth*)0)->top), "Border top member type must match");
_Static_assert(sizeof(uint16_t) == sizeof(((Clay_BorderWidth*)0)->bottom), "Border bottom member type must match");

_Static_assert(offsetof(FlLayoutChildAlignment, x) == offsetof(Clay_ChildAlignment, x),
               "FlLayoutChildAlignment.x offset must match Clay_ChildAlignment.x");
_Static_assert(offsetof(FlLayoutChildAlignment, y) == offsetof(Clay_ChildAlignment, y),
               "FlLayoutChildAlignment.y offset must match Clay_ChildAlignment.y");

_Static_assert(offsetof(FlLayoutSizingMinMax, min) == offsetof(Clay_SizingMinMax, min),
               "FlLayoutSizingMinMax.min offset must match Clay_SizingMinMax.min");
_Static_assert(offsetof(FlLayoutSizingMinMax, max) == offsetof(Clay_SizingMinMax, max),
               "FlLayoutSizingMinMax.max offset must match Clay_SizingMinMax.max");

_Static_assert(offsetof(FlLayoutSizingAxis, size) == offsetof(Clay_SizingAxis, size),
               "FlLayoutSizingAxis.size offset must match Clay_SizingAxis.size");
_Static_assert(offsetof(FlLayoutSizingAxis, type) == offsetof(Clay_SizingAxis, type),
               "FlLayoutSizingAxis.type offset must match Clay_SizingAxis.type");

_Static_assert(offsetof(FlLayoutSizing, width) == offsetof(Clay_Sizing, width),
               "FlLayoutSizing.width offset must match Clay_Sizing.width");
_Static_assert(offsetof(FlLayoutSizing, height) == offsetof(Clay_Sizing, height),
               "FlLayoutSizing.height offset must match Clay_Sizing.height");

_Static_assert(offsetof(FlLayoutPadding, left) == offsetof(Clay_Padding, left),
               "FlLayoutPadding.left offset must match Clay_Padding.left");
_Static_assert(offsetof(FlLayoutPadding, right) == offsetof(Clay_Padding, right),
               "FlLayoutPadding.right offset must match Clay_Padding.right");
_Static_assert(offsetof(FlLayoutPadding, top) == offsetof(Clay_Padding, top),
               "FlLayoutPadding.top offset must match Clay_Padding.top");
_Static_assert(offsetof(FlLayoutPadding, bottom) == offsetof(Clay_Padding, bottom),
               "FlLayoutPadding.bottom offset must match Clay_Padding.bottom");

// FlCornerRadius member offsets - pin the generated type onto Clay's spelling so the
// zero-cost pun (tl/tr/bl/br ↔ topLeft/topRight/bottomLeft/bottomRight) stays valid.
_Static_assert(offsetof(FlCornerRadius, tl) == offsetof(Clay_CornerRadius, topLeft),
               "FlCornerRadius.tl offset must match Clay_CornerRadius.topLeft");
_Static_assert(offsetof(FlCornerRadius, tr) == offsetof(Clay_CornerRadius, topRight),
               "FlCornerRadius.tr offset must match Clay_CornerRadius.topRight");
_Static_assert(offsetof(FlCornerRadius, bl) == offsetof(Clay_CornerRadius, bottomLeft),
               "FlCornerRadius.bl offset must match Clay_CornerRadius.bottomLeft");
_Static_assert(offsetof(FlCornerRadius, br) == offsetof(Clay_CornerRadius, bottomRight),
               "FlCornerRadius.br offset must match Clay_CornerRadius.bottomRight");
