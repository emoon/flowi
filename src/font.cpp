#include <flowi/font.h>
#include <dear-imgui/imgui.h>
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlFont new_from_file(FlInternalData* ctx, FlString filename, uint32_t font_size) {
    char temp_buffer[2048];
    
    ImGuiIO& io = ImGui::GetIO();

    //ImFontAtlas* atlas = ctx->global->font_atlas;
    const char* fname =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), filename);

    ImFont* font = io.Fonts->AddFontFromFileTTF(fname, font_size, NULL, NULL);

    if (!font) {
        ERROR_ADD(FlError_Io, "Unable to convert load filename cstr: %s", fname);
        return 0;
    }

    return (FlFont)font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlFont new_from_memory(FlInternalData* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                               uint32_t font_size) {
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

static void font_push(FlInternalData* ctx, FlFont font) {
    FL_UNUSED(ctx);
    ImGui::PushFont((ImFont*)font);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void font_pop(FlInternalData* ctx) {
    FL_UNUSED(ctx);
    ImGui::PopFont();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destory the current font, render the id invalid

static void destroy(FlInternalData* ctx, FlFont font) { }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlFontApi g_font_funcs = {
    NULL,
    new_from_file,
    new_from_memory,
    font_push,
    font_pop,
    destroy,
};
