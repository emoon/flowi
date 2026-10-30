#include "ui_popup.h"
#include <core/math.h>
#include "external/clay/clay.h"
#include "input_actions.h"
#include "ui_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API

void ui_popup_begin(FlLayoutId anchor_id, FlPopupConfig config) {
    FlUiCtx* ctx = ui_get_ctx();

    u32 anchor_widget_id = ui_layout_id_to_clay_id(anchor_id);

    // An already-open popup keeps its scope id; a fresh one takes the anchor's.
    u32 popup_id = ctx->popup_focus_scope_id ? ctx->popup_focus_scope_id : anchor_widget_id;

    ctx->popup_stack_count++;

    ctx->current_popup_scope_id = popup_id;
    ctx->rendering_popup = true;

    // The first popup saves the focus it displaces.
    if (ui_focus_scope_arm(ctx, popup_id, false)) {
        ctx->popup_open_frame = ctx->frame_counter;
        ctx->popup_needs_initial_focus = true;
        ctx->popup_auto_close = config.auto_close;
    }

    FlLayoutId popup_layout_id = { .name = anchor_id.name, .id = popup_id };
    u32 popup_element_id_value = ui_layout_id_to_clay_id(popup_layout_id);
    Clay_ElementId popup_element_id = { .id = popup_element_id_value };
    Clay__OpenElementWithId(popup_element_id);

    Clay_PointerCaptureMode capture
        = config.capture_input ? CLAY_POINTER_CAPTURE_MODE_CAPTURE : CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;

    Clay_ElementDeclaration decl = { .layout = { .sizing = { .width = { .type = CLAY__SIZING_TYPE_FIT },
                                                             .height = { .type = CLAY__SIZING_TYPE_FIT } } } };

    if (config.anchor == FlPopupAnchor_Point) {
        // Point anchoring: position the popup at an absolute screen coordinate (root-attached + offset), with
        // off-screen avoidance computed here. Sizing is FIT, so the popup's measured box is only known one frame
        // late - the previous frame's box drives flip/clamp. On the very first frame the size is unknown and the
        // popup simply opens downward from the point; it corrects on the next frame.
        Clay_ElementData prev = Clay_GetElementData(popup_element_id);
        float w = prev.found ? prev.boundingBox.width : 0.0f;
        float h = prev.found ? prev.boundingBox.height : 0.0f;
        float sw = ctx->layout_width;
        float sh = ctx->layout_height;

        float px = config.point.x + config.offset.x;
        float py = config.point.y + config.offset.y;

        // Horizontal clamp: keep the right edge on-screen, never pushing the left edge past 0.
        if (w > 0.0f && sw > 0.0f && px + w > sw) {
            px = sw - w;
            if (px < 0.0f) {
                px = 0.0f;
            }
        }

        // Vertical flip-on-overflow: if opening downward would overflow the bottom edge and there is room above,
        // anchor the popup's bottom-left to the point so it opens upward instead. Otherwise clamp downward.
        Clay_FloatingAttachPointType elem_y = CLAY_ATTACH_POINT_LEFT_TOP;
        if (h > 0.0f && py + h > sh && py - h >= 0.0f) {
            elem_y = CLAY_ATTACH_POINT_LEFT_BOTTOM; // bottom-left of popup sits at the point → opens upward
        } else if (h > 0.0f && sh > 0.0f && py + h > sh) {
            py = sh - h;
            if (py < 0.0f) {
                py = 0.0f;
            }
        }

        decl.floating = (Clay_FloatingElementConfig) { .attachTo = CLAY_ATTACH_TO_ROOT,
                                                       .attachPoints
                                                       = { .element = elem_y, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
                                                       .offset = { .x = px, .y = py },
                                                       .zIndex = config.z_index,
                                                       .pointerCaptureMode = capture };
    } else {
        decl.floating = (Clay_FloatingElementConfig) {
            .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
            .parentId = anchor_widget_id,
            .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
            .offset = { .x = config.offset.x, .y = config.offset.y },
            .zIndex = config.z_index,
            .pointerCaptureMode = capture,
        };
    }

    Clay__ConfigureOpenElement(decl);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_popup_end(void) {
    FlUiCtx* ctx = ui_get_ctx();

    Clay__CloseElement();

    ctx->current_popup_scope_id = 0;
    ctx->rendering_popup = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Auto-close logic (called from ui_update_begin, AFTER fl_platform_input_update captures mouse state)

void ui_popup_check_auto_close(FlUiCtx* ctx) {
    if (ctx->popup_focus_scope_id == 0) {
        return;
    }

    if (!ctx->popup_auto_close) {
        return;
    }

    // ESC key, Gamepad B button.
    if (ui_action_pressed(ctx, FlAction_Cancel)) {
        ui_popup_close_internal(ctx);
        return;
    }

    // Click outside, skipped on the frame the popup opened (the click that opened it shouldn't close it).
    if (ctx->mouse_pressed_this_frame && ctx->frame_counter > ctx->popup_open_frame) {
        bool clicked_inside_popup = false;

        if (ctx->hot_id != 0) {
            ItemState* hot_item = hashmap_get(&ctx->shared->item_state, ctx->hot_id);
            if (hot_item != nullptr && hot_item->popup_scope_id == ctx->popup_focus_scope_id) {
                clicked_inside_popup = true;
            }
        }

        if (!clicked_inside_popup) {
            ui_popup_close_internal(ctx);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_popup_close_internal(FlUiCtx* ctx) {
    ui_focus_scope_release(ctx);

    ctx->popup_open_frame = 0;
    ctx->rendering_popup = false;
    ctx->popup_closed_this_frame = true;

    ctx->popup_stack_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_focus_scope_arm(FlUiCtx* ctx, u32 scope_id, bool take_over) {
    bool arm = take_over ? ctx->popup_focus_scope_id != scope_id : ctx->popup_focus_scope_id == 0;
    if (arm) {
        ctx->saved_focused_id = ctx->focused_id;
        ctx->popup_focus_scope_id = scope_id;
    }
    return arm;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_focus_scope_release(FlUiCtx* ctx) {
    ctx->focused_id = ctx->saved_focused_id;
    ctx->popup_focus_scope_id = 0;
    ctx->current_popup_scope_id = 0;
}
