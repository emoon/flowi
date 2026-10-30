#include "gpu_svg_translate.h"
#include "gpu_svg_atlas.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlCmdImage gpu_svg_image_cmd(FlImageHandle handle, GpuAtlasCell cell, FlVectorClass cls, FlColor tint, FlRect dst) {
    // Content sub-rect: drop the transparent padding border the cell carries for clean linear sampling.
    FlRect src = {
        .x = (f32)(cell.x + GPU_SVG_ATLAS_PAD),
        .y = (f32)(cell.y + GPU_SVG_ATLAS_PAD),
        .w = (f32)(cell.w - 2 * GPU_SVG_ATLAS_PAD),
        .h = (f32)(cell.h - 2 * GPU_SVG_ATLAS_PAD),
    };

    bool mono = cls == FlVectorClass_Monochrome;

    return (FlCmdImage) {
        .source = { .kind = FlImageKind_Static, .handle = handle },
        .mode = mono ? FlImageMode_AlphaCoverage : FlImageMode_Color, // mono is tintable; color is RGBA
        .dst = dst,
        .src = src,
        .tint = mono ? tint : (FlColor) { 255, 255, 255, 255 }, // color: white = no modulation
        .filter = FlImageFilter_Linear,
    };
}
