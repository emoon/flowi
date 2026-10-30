#include "gpu_cpu_state.h"
#include "gpu_debug.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void gpu_release_image(void* user_data, FlImageHandle handle) {
    GpuCpuState* cpu = user_data;
    gpu_image_atlas_release(&cpu->image_atlas, handle);
    gpu_image_slots_release_static(&cpu->image_slots, handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug seams (gpu_debug.h) answered from the CPU bookkeeping alone, identical on every backend.

const GpuGlyphEntry* gpu_debug_find_glyph(void* renderer_user_data, FlFontId font, u16 size, u32 glyph_id) {
    GpuCpuState* cpu = renderer_user_data;
    return gpu_glyph_atlas_find(&cpu->glyph_atlas, font, size, glyph_id);
}

const GpuSvgEntry* gpu_debug_find_svg(void* renderer_user_data, FlImageHandle handle) {
    GpuCpuState* cpu = renderer_user_data;
    return gpu_svg_atlas_find(&cpu->svg_atlas, handle);
}

const GpuSvgEntry* gpu_debug_find_color_svg(void* renderer_user_data, FlImageHandle handle) {
    GpuCpuState* cpu = renderer_user_data;
    return gpu_svg_atlas_find(&cpu->color_svg_atlas, handle);
}

u32 gpu_debug_svg_raster_count(void* renderer_user_data) {
    GpuCpuState* cpu = renderer_user_data;
    return cpu->svg_raster_count;
}

u32 gpu_debug_svg_compact_count(void* renderer_user_data) {
    GpuCpuState* cpu = renderer_user_data;
    return cpu->svg_compact_count;
}

u32 gpu_debug_dynamic_upload_count(void* renderer_user_data) {
    GpuCpuState* cpu = renderer_user_data;
    return cpu->dynamic_upload_count;
}

bool gpu_debug_image_in_atlas(void* renderer_user_data, FlImageHandle handle) {
    GpuCpuState* cpu = renderer_user_data;
    return gpu_image_atlas_find(&cpu->image_atlas, handle) != NULL;
}
