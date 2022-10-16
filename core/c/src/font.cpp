#include <flowi_core/font.h>
#include "../external/imgui/imgui.h"
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" FlFont fl_font_new_from_file_impl(struct FlContext* ctx, FlString filename, uint32_t font_size) {
    char temp_buffer[2048];

    ImFontAtlas* atlas = ctx->global->font_atlas;
    const char* fname =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), filename);

    ImFont* font = atlas->AddFontFromFileTTF(fname, font_size, NULL, NULL);

    if (!font) {
        ERROR_ADD(FlError_Io, "Unable to convert load filename cstr: %s", fname);
        return 0;
    }

    return (FlFont)font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" FlFont fl_font_new_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                               uint32_t font_size) {
    ImFontAtlas* atlas = ctx->global->font_atlas;
    // TODO: Note: Transfer ownership of 'ttf_data' to ImFontAtlas! Will be deleted after destruction of the atlas. Set
    // font_cfg->FontDataOwnedByAtlas=false to keep ownership of your data and it won't be freed.
    ImFont* font = atlas->AddFontFromMemoryTTF(data, data_size, font_size);

    if (!font) {
        ERROR_ADD(FlError_Io, "Unable to convert load filename cstr: %S", name);
        return 0;
    }

    return (FlFont)font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destory the current font, render the id invalid

extern "C" void fl_font_destroy_impl(struct FlContext* ctx, FlFont font) {
}
