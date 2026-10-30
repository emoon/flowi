#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: the CPU-side placement bookkeeping and work counters both the GL and Metal backends own. A backend
// embeds one as the first member of its shared struct, so the backend's user_data pointer is also a GpuCpuState
// pointer for the backend-agnostic seams (gpu_release_image, the gpu_debug_* lookups).

#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_svg_atlas.h"
#include <core/types.h>

typedef struct GpuCpuState {
    GpuGlyphAtlas glyph_atlas;   // (font, size, glyph) -> atlas cell + metrics
    GpuImageAtlas image_atlas;   // FlImageHandle -> atlas cell
    GpuImageSlots image_slots;   // large statics and dynamic buffers on their own textures
    GpuSvgAtlas svg_atlas;       // monochrome / coverage (R8)
    GpuSvgAtlas color_svg_atlas; // baked multi-color (RGBA8)

    u32 svg_raster_count;     // monotonic since create; ticks once per SVG cell-raster dispatch
    u32 svg_compact_count;    // monotonic since create; ticks once per atlas-full wholesale reclaim
    u32 dynamic_upload_count; // monotonic since create; ticks once per dynamic slot entry streamed on a
                              // referencing frame
} GpuCpuState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Drop a prepared image's representation: an atlas cell frees for reuse, a slot entry queues its texture on the
// dead list. A handle lives in exactly one tier, so both releases run and the other is a no-op. The render_backend
// release_image seam for both backends; user_data is the backend's shared struct.
void gpu_release_image(void* user_data, FlImageHandle handle);
