#include <flowi/font.h>
#include <dear-imgui/imgui.h>
#include "internal.h"
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_PUBLIC_SYMBOL FlFont fl_font_load_with_range_impl(FlInternalData* ctx, FlString filename, uint32_t font_size, 
                                                    uint16_t start, uint16_t end) {
    char temp_buffer[2048];
    uint16_t ranges[] = { start, end, 0 };
    uint16_t* range = ranges;

    if (start == 0 && end == 0)
        range = nullptr;
    else {
        // TODO: fixme
        range = new uint16_t[3];
        range[0] = start;
        range[1] = end;
        range[2] = 0;
    }
    
    ImGuiIO& io = ImGui::GetIO();

    const char* fname =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), filename);

    ImFont* font = io.Fonts->AddFontFromFileTTF(fname, font_size, NULL, range);

    if (!font) {
        printf("Unable to convert load filename cstr: %s\n", fname);
        return 0;
    }

    return (FlFont)font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_PUBLIC_SYMBOL FlFont fl_font_load_impl(FlInternalData* ctx, FlString filename, uint32_t font_size) {
    return fl_font_load_with_range_impl(ctx, filename, font_size, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_PUBLIC_SYMBOL FlFont fl_font_load_from_memory_impl(FlInternalData* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                               uint32_t font_size) {
    FL_UNUSED(ctx);

    ImGuiIO& io = ImGui::GetIO();

    //ImFontAtlas* atlas = ctx->global->font_atlas;
    // TODO: Note: Transfer ownership of 'ttf_data' to ImFontAtlas! Will be deleted after destruction of the atlas. Set
    // font_cfg->FontDataOwnedByAtlas=false to keep ownership of your data and it won't be freed.
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(data, data_size, font_size);

    if (!font) {
        ERROR_ADD(FlError_Io, "Unable to convert load filename cstr: %S", name);
        return 0;
    }

    return (FlFont)font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_PUBLIC_SYMBOL void fl_font_push_impl(FlInternalData* ctx, FlFont font) {
    FL_UNUSED(ctx);
    ImGui::PushFont((ImFont*)font);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_PUBLIC_SYMBOL void fl_font_pop_impl(FlInternalData* ctx) {
    FL_UNUSED(ctx);
    ImGui::PopFont();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destory the current font, render the id invalid

FL_PUBLIC_SYMBOL void fl_font_destroy_impl(FlInternalData* ctx, FlFont font) { 
    FL_UNUSED(ctx);
    FL_UNUSED(font);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlFontApi g_font_funcs = {
    NULL,
    fl_font_load_impl,
    fl_font_load_with_range_impl,
    fl_font_load_from_memory_impl,
    fl_font_push_impl,
    fl_font_pop_impl,
    fl_font_destroy_impl,
};
