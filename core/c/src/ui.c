#include <flowi_core/math_data.h>
#include <flowi_core/style.h>
#include <flowi_core/ui.h>
#include "font_private.h"
#include "image_private.h"
#include "internal.h"
#include "style_internal.h"
#include "text.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_mouse_hovering_rect(const FlContext* ctx, FlRect rect) {
    // TODO: Math function
    // TODO: SIMD
    FlVec2 pos = ctx->mouse.pos;

    if (pos.x >= rect.x && pos.y >= rect.y) {
        FlVec2 pos2 = (FlVec2){rect.x + rect.width, rect.y + rect.height};

        if (pos.x < pos2.x && pos.y < pos2.y) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool item_hoverable(FlContext* ctx, FlRect rect, FlowiID id) {
    printf("hovered id %d\n", ctx->hovered_id);

    if (ctx->hovered_id != 0 && ctx->hovered_id != id) {
        return false;
    }

    if (ctx->active_id != 0 && ctx->active_id != id) {
        return false;
    }

    if (!is_mouse_hovering_rect(ctx, rect)) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void set_active_id(FlContext* ctx, FlowiID id) {
    ctx->active_id_is_just_activated = ctx->active_id != id;

    if (ctx->active_id_is_just_activated) {
        ctx->active_id_timer = 0.0f;
        ctx->active_id_has_been_pressed_before = false;
        ctx->active_id_has_been_edited_before = false;
        if (id != 0) {
            ctx->last_active_id = id;
            ctx->last_active_id_timer = 0.0f;
        }
    }

    ctx->active_id = id;
    ctx->active_id_allow_overlap = false;
    ctx->active_id_no_clear_on_focus_loss = false;
    ctx->active_id_has_been_edited_this_frame = false;

    if (id) {
        ctx->active_id_is_alive = id;
        /*
        ctx->active_id_source =
            (ctx->nav_activate_id == id ||
             ctx->nav_input_id == id ||
             ctx->nav_just_tabbed_id == id ||
             ctx->nav_just_moved_to_id  == id) ? InputSource_Nav : InputSource_Mouse;
        */
    }

    // ctx->active_id_window = window;
    // Clear declaration of inputs claimed by the widget
    // (Please note that this is WIP and not all keys/inputs are thoroughly declared by all widgets yet)
    // ctx->active_id_using_mouse_wheel = false;
    // ctx->active_id_using_nav_dir_mask = 0x00;
    // ctx->active_id_using_nav_input_mask = 0x00;
    // ctx->active_id_using_key_input_mask = 0x00;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clear_active_id(FlContext* ctx) {
    set_active_id(ctx, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool button_behavior(struct FlContext* ctx, FlRect rect, FlowiID id, u32 flags) {
    const MouseState* mouse_state = &ctx->mouse;

    bool pressed = false;
    bool hovered = item_hoverable(ctx, rect, id);

    // Mouse handling
    if (hovered) {
        // printf("is hovered\n");
        //  Poll buttons
        int button_clicked = -1;

        if ((flags & ButtonFlags_MouseButtonLeft) && mouse_state->clicked[0]) {
            button_clicked = 1;
        }

        // print
        // if (button_clicked) {
        //     printf("button clicked %d\n", button_clicked);
        // }

        if (button_clicked != -1 && ctx->active_id != id) {
            if (flags & (ButtonFlags_PressedOnClickRelease | ButtonFlags_PressedOnClickReleaseAnywhere)) {
                set_active_id(ctx, id);
            }

            const bool pressed_on_click = flags & ButtonFlags_PressedOnClick;
            const bool pressed_on_double_click = flags & ButtonFlags_PressedOnDoubleClick;
            const bool double_clicked = mouse_state->double_clicked[button_clicked];

            if (pressed_on_click || (pressed_on_double_click && double_clicked)) {
                pressed = true;
                set_active_id(ctx, id);
            }
        }

        if (flags & ButtonFlags_PressedOnRelease) {
            // int button_released = -1;

            // Repeat mode trumps on release behavior
            // const bool has_repeated_at_least_once = false;
            // if (!has_repeated_at_least_once)

            clear_active_id(ctx);
        }
    }

    if (mouse_state->released[0]) {
        pressed = true;
    }

    // Process while held
    // bool held = false;
    if (ctx->active_id == id) {
        // if (g.active_id_source == InputSource_Mouse)
        // always mouse for now
        if (true) {
            /*
            if (ctx->active_id_is_just_activated) {
                // g.active_id_click_offset = vec2_sub(ctx->mouse_state.pos, rect.min);
            }
            */

            const int mouse_button = ctx->active_id_mouse_button;

            if (ctx->mouse.down[mouse_button]) {
                // held = true;
            } else {
                bool release_in = hovered && (flags & ButtonFlags_PressedOnClickRelease) != 0;
                bool release_anywhere = (flags & ButtonFlags_PressedOnClickReleaseAnywhere) != 0;
                const float repeat_delay = 0.025f;  // TODO: Configurable value
                // if ((release_in || release_anywhere) && !ctx->drag_drop_active) {
                if (release_in || release_anywhere) {
                    // Report as pressed when releasing the mouse (this is the most common path)
                    bool is_double_click_release =
                        (flags & ButtonFlags_PressedOnDoubleClick) && ctx->mouse.down_was_double_click[mouse_button];
                    // Repeat mode trumps <on release>
                    bool is_repeating_already =
                        (flags & ButtonFlags_Repeat) && ctx->mouse.down_duration_prev[mouse_button] >= repeat_delay;
                    if (!is_double_click_release && !is_repeating_already)
                        pressed = true;
                }
                clear_active_id(ctx);
            }
        }
        // if (!(flags & ButtonFlags_NoNavFocus))
        //     ctx->nav_disable_highlight = true;
    }
    /*
    else if (g.active_id_source == im_gui_input_source_nav) {
        // When activated using Nav, we hold on the ActiveID until activation button is released
        if (ctx->nav_activate_down_id != id)
            clear_active_id();
    }
    if (pressed) {
        g.active_id_has_been_pressed_before = true;
    }
    */

    if (pressed) {
        printf("pressed\n");
    }

    return pressed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// taken from:
// http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/142054
//
// djb  :: 99.8601 percent coverage (60 collisions out of 42884)
// elf  :: 99.5430 percent coverage (196 collisions out of 42884)
// sdbm :: 100.0000 percent coverage (0 collisions out of 42884) (this is the algo used)
// ...

static u32 str_hash(const char* string, int len) {
    u32 hash = 0;

    const u8* str = (const u8*)string;

    for (int i = 0; i < len; ++i) {
        u32 c = *str++;
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash & 0x7FFFFFFF;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)

void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos) {
    ctx->cursor = pos;
}

/*
// Declare item bounding box for clipping and interaction.
// Note that the size can be different than the one provided to ItemSize(). Typically, widgets that spread over
available surface
// declare their minimum size requirement to ItemSize() and provide a larger region to ItemAdd() which is used
drawing/interaction. bool ImGui::ItemAdd(const ImRect& bb, ImGuiID id, const ImRect* nav_bb_arg, ImGuiItemFlags
extra_flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    // Set item data
    // (DisplayRect is left untouched, made valid when ImGuiItemStatusFlags_HasDisplayRect is set)
    g.LastItemData.ID = id;
    g.LastItemData.Rect = bb;
    g.LastItemData.NavRect = nav_bb_arg ? *nav_bb_arg : bb;
    g.LastItemData.InFlags = g.CurrentItemFlags | extra_flags;
    g.LastItemData.StatusFlags = ImGuiItemStatusFlags_None;

    // Directional navigation processing
    if (id != 0)
    {
        KeepAliveID(id);

        // Runs prior to clipping early-out
        //  (a) So that NavInitRequest can be honored, for newly opened windows to select a default widget
        //  (b) So that we can scroll up/down past clipped items. This adds a small O(N) cost to regular navigation
requests
        //      unfortunately, but it is still limited to one window. It may not scale very well for windows with ten of
        //      thousands of item, but at least NavMoveRequest is only set on user interaction, aka maximum once a
frame.
        //      We could early out with "if (is_clipped && !g.NavInitRequest) return false;" but when we wouldn't be
able
        //      to reach unclipped widgets. This would work if user had explicit scrolling control (e.g. mapped on a
stick).
        // We intentionally don't check if g.NavWindow != NULL because g.NavAnyRequest should only be set when it is non
null.
        // If we crash on a NULL g.NavWindow we need to fix the bug elsewhere.
        window->DC.NavLayersActiveMaskNext |= (1 << window->DC.NavLayerCurrent);
        if (g.NavId == id || g.NavAnyRequest)
            if (g.NavWindow->RootWindowForNav == window->RootWindowForNav)
                if (window == g.NavWindow || ((window->Flags | g.NavWindow->Flags) & ImGuiWindowFlags_NavFlattened))
                    NavProcessItem();

        // [DEBUG] People keep stumbling on this problem and using "" as identifier in the root of a window instead of
"##something".
        // Empty identifier are valid and useful in a small amount of cases, but 99.9% of the time you want to use
"##something".
        // READ THE FAQ: https://dearimgui.org/faq
        IM_ASSERT(id != window->ID && "Cannot have an empty ID at the root of a window. If you need an empty label, use
## and read the FAQ about how the ID Stack works!");

        // [DEBUG] Item Picker tool, when enabling the "extended" version we perform the check in ItemAdd()
#ifdef IMGUI_DEBUG_TOOL_ITEM_PICKER_EX
        if (id == g.DebugItemPickerBreakId)
        {
            IM_DEBUG_BREAK();
            g.DebugItemPickerBreakId = 0;
        }
#endif
    }
    g.NextItemData.Flags = ImGuiNextItemDataFlags_None;

#ifdef IMGUI_ENABLE_TEST_ENGINE
    if (id != 0)
        IMGUI_TEST_ENGINE_ITEM_ADD(nav_bb_arg ? *nav_bb_arg : bb, id);
#endif

    // Clipping test
    const bool is_clipped = IsClippedEx(bb, id);
    if (is_clipped)
        return false;
    //if (g.IO.KeyAlt) window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(255,255,0,120)); // [DEBUG]

    // We need to calculate this now to take account of the current clipping rectangle (as items like Selectable may
change them) if (IsMouseHoveringRect(bb.Min, bb.Max)) g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredRect;
    return true;
}
*/

static void keep_id_alive(struct FlContext* ctx, FlowiID id) {
    if (ctx->active_id == id) {
        ctx->active_id_is_alive = true;
    }

    if (ctx->active_id_previous_frame == id) {
        ctx->active_id_previous_frame_is_alive = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool item_add(struct FlContext* ctx, FlowiID id, FlRect rect) {
    ctx->last_item_data.id = id;
    ctx->last_item_data.rect = rect;
    ctx->last_item_data.status_flags = 0;

    if (id != 0) {
        keep_id_alive(ctx, id);
    }

    return is_mouse_hovering_rect(ctx, rect);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push button widget that returns true if user has pressed it

bool fl_ui_push_button_impl(struct FlContext* ctx, FlString text) {
    FlStyle style = fl_style_get_current(ctx);

    Utf8Result utf8_res = Utf8_to_codepoints_u32(&ctx->frame_allocator, (u8*)text.str, text.len);

    if (utf8_res.error == FlError_Utf8Malformed) {
        // TODO: Proper error
        printf("illegal string\n");
        return false;
    }

    FlowiID id = (FlowiID)str_hash(text.str, text.len);

    FlVec2 pos = ctx->cursor;

    pos.x += style.margin[FlCorner_TopLeft];
    pos.y += style.margin[FlCorner_TopRight];

    // TODO: text alignment (currently assume text should be centered)

    // Calculate the size of the text
    FlIVec2 text_size = Font_calc_text_size(ctx, utf8_res.codepoints, utf8_res.len);

    // TODO: Handle corners correctly
    uint16_t x_padding = style.padding[FlCorner_TopRight] + style.padding[FlCorner_BottomRight];
    uint16_t y_padding = style.padding[FlCorner_TopLeft] + style.padding[FlCorner_BottomLeft];
    FlVec2 box_size = {text_size.x + x_padding, text_size.y + y_padding};

    // static bool button_behavior(struct FlContext * ctx, FlRect rect, FlowiID id, u32 flags);
    FlRect rect = {.x = (int)pos.x, .y = (int)pos.y, .width = (int)box_size.x, .height = (int)box_size.y};

    // Add box for rendering
    {
        PrimitiveBox* prim = Primitive_alloc_box(ctx->global);
        prim->pos = pos;
        prim->size = box_size;
        prim->color = 0x00ffffff;
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(ctx->global);

        prim->font = ctx->current_font;
        prim->position.x = pos.x + style.padding[FlCorner_TopLeft];
        prim->position.y = pos.y + style.padding[FlCorner_TopRight];
        prim->font_size = ctx->current_font_size != 0 ? ctx->current_font_size : ctx->current_font->default_size;
        prim->codepoints = utf8_res.codepoints;
        prim->codepoint_count = utf8_res.len;
        prim->position_index = 0;  // TODO: Fixme
    }

    // TODO: Move cursor directly downwards with style curently, need to configure?
    ctx->cursor.y += text_size.y + y_padding + style.margin[FlCorner_TopLeft] + style.margin[FlCorner_TopRight];

    if (!item_add(ctx, id, rect)) {
        return false;
    }

    printf("is hoviring rect 0x%x\n", id);

    return button_behavior(ctx, rect, id, ButtonFlags_PressedOnClickRelease | ButtonFlags_MouseButtonLeft);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_ui_push_button_with_icon_impl(struct FlContext* ctx, FlString text, FlImage image, FlVec2 text_pos,
                                      float image_scale) {
    Utf8Result utf8_res = Utf8_to_codepoints_u32(&ctx->frame_allocator, (u8*)text.str, text.len);

    if (utf8_res.error == FlError_Utf8Malformed) {
        // TODO: Proper error
        printf("illegal string\n");
        return false;
    }

    ImagePrivate* image_data = (ImagePrivate*)Handles_get_data(&ctx->global->image_handles, image);
    FL_UNUSED(image_data);

    if (!image) {
        // printf("Illegal image id %lx\n", (u64)image);
        return false;
    }

    // Calculate the size of the text
    FlIVec2 text_size = Font_calc_text_size(ctx, utf8_res.codepoints, utf8_res.len);

    FlIVec2 image_size = {
        (int)(image_data->info.width * image_scale),
        (int)(image_data->info.height * image_scale),
    };

    // adjust the text a bit to center with image
    int text_offset = (image_size.y - text_size.y);  // * 0.5f;

    FlVec2 pos = ctx->cursor;

    // Add image for rendering
    {
        PrimitiveImage* prim = Primitive_alloc_image(ctx->global);
        prim->image = image_data;
        prim->position = pos;
        prim->size.x = image_size.x;
        prim->size.y = image_size.y;
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(ctx->global);

        prim->font = ctx->current_font;
        prim->position.x = pos.x + text_pos.x;
        prim->position.y = pos.y + text_pos.y + text_offset;
        prim->font_size = ctx->current_font_size != 0 ? ctx->current_font_size : ctx->current_font->default_size;
        prim->codepoints = utf8_res.codepoints;
        prim->codepoint_count = utf8_res.len;
        prim->position_index = 0;  // TODO: Fixme
    }

    // TODO: Include style in position calculation

    // Based on the current the the position where we should place the image and the text
    // FlVec2 pos = Style_get_position(ctx);

    return false;
}
