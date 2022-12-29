#include <flowi/ui.h>
#include <flowi/style.h>
#include "image_private.h"
#include "error.h"
#include "primitives.h"
#include "layer.h"
#include "../external/dear-imgui/imgui.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// we include the dear-imgui code here to get better inilining/codegen/code removal as the bellow code
// is the interface between flowi and dear-imgui
//

/*
// Can be used for extra opt 
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../external/dear-imgui/imgui.cpp"
#include "../external/dear-imgui/imgui_draw.cpp"
#include "../external/dear-imgui/imgui_widgets.cpp"
#include "../external/dear-imgui/imgui_tables.cpp"
*/
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// translate from FlColor to ImGuiCol_

static int s_color_lut[ImGuiCol_COUNT * 4];
static int s_single_style_lut[ImGuiStyleVar_COUNT];
static int s_vec2_style_lut[ImGuiStyleVar_COUNT];

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool begin(struct FlContext* ctx, FlString name, FlWindowFlags flags) {
    char temp_buffer[2048];

    const char* window_name =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), name);

    return ImGui::Begin(window_name, NULL, (ImGuiWindowFlags)flags);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void end(struct FlContext* ctx) {
    FL_UNUSED(ctx);
    ImGui::End();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void image(struct FlContext* ctx, FlImage image) {
    ImagePrivate* image_data = (ImagePrivate*)Handles_get_data(&ctx->global->image_handles, image);

    if (!image_data) {
        ERROR_ADD(FlError_Image, "Invalid handle %s", "todo name");
        return;
    }

    // TODO: Better way to do this?
    PrimitiveImage* prim = (PrimitiveImage*)CommandBuffer_alloc_cmd(
            &ctx->layers[0].primitive_commands, 
            Primitive_DrawImage, 
            sizeof(PrimitiveImage));
    prim->image = image_data;
    prim->size.x = image_data->info.width;
    prim->size.y = image_data->info.height;

    ImGui::Image((ImTextureID)image, ImVec2(image_data->info.width, image_data->info.height),
                 ImVec2(image_data->u0, image_data->v0), ImVec2(image_data->u1, image_data->v1));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Permantly set a color

extern "C" void fl_style_set_color_impl(struct FlContext* ctx, FlStyleColor color, FlColor value) {
    FL_UNUSED(ctx);
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    int color_index = s_color_lut[color];
    colors[color_index] = ImVec4(value.r, value.g, value.b, value.a);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Permantly set a color (RGBA)

extern "C" void fl_style_set_color_u32_impl(struct FlContext* ctx, FlStyleColor color, uint32_t value) {
    FL_UNUSED(ctx);
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    int color_index = s_color_lut[color];
    colors[color_index] = ImGui::ColorConvertU32ToFloat4(value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Temporary push a color change (RGBA)

extern "C" void fl_style_push_color_u32_impl(struct FlContext* ctx, FlStyleColor color, uint32_t value) {
    FL_UNUSED(ctx);
    int color_index = s_color_lut[color];
    ImGui::PushStyleColor(color_index, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Temporary push a color change

extern "C" void fl_style_push_color_impl(struct FlContext* ctx, FlStyleColor color, FlColor value) {
    FL_UNUSED(ctx);
    int color_index = s_color_lut[color];
    ImGui::PushStyleColor(color_index, ImVec4(value.r, value.g, value.b, value.a));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Temporary push a color change

extern "C" void fl_style_pop_color_impl(struct FlContext* ctx) {
    ImGui::PopStyleColor();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pushes a single style change

extern "C" void fl_style_push_single_impl(struct FlContext* ctx, FlStyleSingle style, float value) {
    FL_UNUSED(ctx);
    int style_index = s_single_style_lut[style];
    ImGui::PushStyleVar(style_index, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pushes a Vec2 style change

extern "C" void fl_style_push_vec2_impl(struct FlContext* ctx, FlStyleVec2 style, FlVec2 value) {
    FL_UNUSED(ctx);
    int style_index = s_vec2_style_lut[style];
    ImGui::PushStyleVar(style_index, ImVec2(value.x, value.y));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pops single style change

extern "C" void fl_style_pop_impl(struct FlContext* ctx) {
    FL_UNUSED(ctx);
    ImGui::PopStyleVar();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlUiApi g_ui_funcs = {
    NULL,
    begin,
    end,
    NULL, // TODO: Fix me
    image,
    NULL,
    NULL, 
    NULL,
    NULL,
    NULL,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" FlUiApi* fl_ui_get_api(struct FlContext* ctx, int api_version) {
    FL_UNUSED(api_version);
    return &ctx->ui_funcs;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int s_imgui_colors[] = {
    ImGuiCol_Text,
    ImGuiCol_TextDisabled,
    ImGuiCol_WindowBg,
    ImGuiCol_ChildBg,
    ImGuiCol_PopupBg,
    ImGuiCol_Border,
    ImGuiCol_BorderShadow,
    ImGuiCol_FrameBg,
    ImGuiCol_FrameBgHovered,
    ImGuiCol_FrameBgActive,
    ImGuiCol_TitleBg,
    ImGuiCol_TitleBgActive,
    ImGuiCol_TitleBgCollapsed,
    ImGuiCol_MenuBarBg,
    ImGuiCol_ScrollbarBg,
    ImGuiCol_ScrollbarGrab,
    ImGuiCol_ScrollbarGrabHovered,
    ImGuiCol_ScrollbarGrabActive,
    ImGuiCol_CheckMark,
    ImGuiCol_SliderGrab,
    ImGuiCol_SliderGrabActive,
    ImGuiCol_Button,
    ImGuiCol_ButtonHovered,
    ImGuiCol_ButtonActive,
    ImGuiCol_Header,
    ImGuiCol_HeaderHovered,
    ImGuiCol_HeaderActive,
    ImGuiCol_Separator,
    ImGuiCol_SeparatorHovered,
    ImGuiCol_SeparatorActive,
    ImGuiCol_ResizeGrip,
    ImGuiCol_ResizeGripHovered,
    ImGuiCol_ResizeGripActive,
    ImGuiCol_Tab,
    ImGuiCol_TabHovered,
    ImGuiCol_TabActive,
    ImGuiCol_TabUnfocused,
    ImGuiCol_TabUnfocusedActive,
    ImGuiCol_DockingPreview,
    ImGuiCol_DockingEmptyBg,
    ImGuiCol_PlotLines,
    ImGuiCol_PlotLinesHovered,
    ImGuiCol_PlotHistogram,
    ImGuiCol_PlotHistogramHovered,
    ImGuiCol_TableHeaderBg,
    ImGuiCol_TableBorderStrong,
    ImGuiCol_TableBorderLight,
    ImGuiCol_TableRowBg,
    ImGuiCol_TableRowBgAlt,
    ImGuiCol_TextSelectedBg,
    ImGuiCol_DragDropTarget,
    ImGuiCol_NavHighlight,
    ImGuiCol_NavWindowingHighlight,
    ImGuiCol_NavWindowingDimBg,
    ImGuiCol_ModalWindowDimBg,
};

static int s_flowi_colors[] = {
    FlStyleColor_Text,
    FlStyleColor_TextDisabled,
    FlStyleColor_WindowBg,
    FlStyleColor_ChildBg,
    FlStyleColor_PopupBg,
    FlStyleColor_Border,
    FlStyleColor_BorderShadow,
    FlStyleColor_FrameBg,
    FlStyleColor_FrameBgHovered,
    FlStyleColor_FrameBgActive,
    FlStyleColor_TitleBg,
    FlStyleColor_TitleBgActive,
    FlStyleColor_TitleBgCollapsed,
    FlStyleColor_MenuBarBg,
    FlStyleColor_ScrollbarBg,
    FlStyleColor_ScrollbarGrab,
    FlStyleColor_ScrollbarGrabHovered,
    FlStyleColor_ScrollbarGrabActive,
    FlStyleColor_CheckMark,
    FlStyleColor_SliderGrab,
    FlStyleColor_SliderGrabActive,
    FlStyleColor_Button,
    FlStyleColor_ButtonHovered,
    FlStyleColor_ButtonActive,
    FlStyleColor_Header,
    FlStyleColor_HeaderHovered,
    FlStyleColor_HeaderActive,
    FlStyleColor_Separator,
    FlStyleColor_SeparatorHovered,
    FlStyleColor_SeparatorActive,
    FlStyleColor_ResizeGrip,
    FlStyleColor_ResizeGripHovered,
    FlStyleColor_ResizeGripActive,
    FlStyleColor_Tab,
    FlStyleColor_TabHovered,
    FlStyleColor_TabActive,
    FlStyleColor_TabUnfocused,
    FlStyleColor_TabUnfocusedActive,
    FlStyleColor_DockingPreview,
    FlStyleColor_DockingEmptyBg,
    FlStyleColor_PlotLines,
    FlStyleColor_PlotLinesHovered,
    FlStyleColor_PlotHistogram,
    FlStyleColor_PlotHistogramHovered,
    FlStyleColor_TableHeaderBg,
    FlStyleColor_TableBorderStrong,
    FlStyleColor_TableBorderLight,
    FlStyleColor_TableRowBg,
    FlStyleColor_TableRowBgAlt,
    FlStyleColor_TextSelectedBg,
    FlStyleColor_DragDropTarget,
    FlStyleColor_NavHighlight,
    FlStyleColor_NavWindowingHighlight,
    FlStyleColor_NavWindowingDimBg,
    FlStyleColor_ModalWindowDimBg,
};

static int s_fl_single_styles[] = {
    FlStyleSingle_Alpha,
    FlStyleSingle_DisabledAlpha,
    FlStyleSingle_WindowRounding,
    FlStyleSingle_WindowBorderSize,
    FlStyleSingle_ChildRounding,
    FlStyleSingle_ChildBorderSize,
    FlStyleSingle_PopupRounding,
    FlStyleSingle_PopupBorderSize,
    FlStyleSingle_FrameRounding,
    FlStyleSingle_FrameBorderSize,
    FlStyleSingle_IndentSpacing,
    FlStyleSingle_ScrollbarSize,
    FlStyleSingle_ScrollbarRounding,
    FlStyleSingle_GrabMinSize,
    FlStyleSingle_GrabRounding,
    FlStyleSingle_TabRounding,
};

static int s_imgui_single_styles[] = {
    ImGuiStyleVar_Alpha,
    ImGuiStyleVar_DisabledAlpha,
    ImGuiStyleVar_WindowRounding,
    ImGuiStyleVar_WindowBorderSize,
    ImGuiStyleVar_ChildRounding,
    ImGuiStyleVar_ChildBorderSize,
    ImGuiStyleVar_PopupRounding,
    ImGuiStyleVar_PopupBorderSize,
    ImGuiStyleVar_FrameRounding,
    ImGuiStyleVar_FrameBorderSize,
    ImGuiStyleVar_IndentSpacing,
    ImGuiStyleVar_ScrollbarSize,
    ImGuiStyleVar_ScrollbarRounding,
    ImGuiStyleVar_GrabMinSize,
    ImGuiStyleVar_GrabRounding,
    ImGuiStyleVar_TabRounding,
};

static int s_fl_vec2_styles[] = {
    FlStyleVec2_WindowPadding,
    FlStyleVec2_WindowMinSize,
    FlStyleVec2_WindowTitleAlign,
    FlStyleVec2_FramePadding,
    FlStyleVec2_ItemSpacing,
    FlStyleVec2_ItemInnerSpacing,
    FlStyleVec2_CellPadding,
    FlStyleVec2_ButtonTextAlign,
    FlStyleVec2_SelectableTextAlign,
};

static int s_imgui_vec2_styles[] = {
    ImGuiStyleVar_WindowPadding,
    ImGuiStyleVar_WindowMinSize,
    ImGuiStyleVar_WindowTitleAlign,
    ImGuiStyleVar_FramePadding,
    ImGuiStyleVar_ItemSpacing,
    ImGuiStyleVar_ItemInnerSpacing,
    ImGuiStyleVar_CellPadding,
    ImGuiStyleVar_ButtonTextAlign,
    ImGuiStyleVar_SelectableTextAlign,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_style_init_priv() {
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        s_color_lut[s_imgui_colors[i]] = s_flowi_colors[i];
    }

    for (int i = 0; i < FL_SIZEOF_ARRAY(s_fl_single_styles); ++i) {
        s_single_style_lut[s_fl_single_styles[i]] = s_imgui_single_styles[i];
    }

    for (int i = 0; i < FL_SIZEOF_ARRAY(s_fl_vec2_styles); ++i) {
        s_vec2_style_lut[s_fl_vec2_styles[i]] = s_imgui_vec2_styles[i];
    }
}

