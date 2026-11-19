// vector image parse, lower and svg atlas tests

#include "render/common.h"

static const char k_svg_stroke[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#00ff00\" stroke=\"#000000\" stroke-width=\"3\"/>"
      "</svg>";

// One valid solid-fill shape followed by one gradient shape.
static const char k_svg_mixed[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" gradientUnits=\"userSpaceOnUse\">"
      "<stop offset=\"0\" stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/></linearGradient></defs>"
      "<rect x=\"0\" y=\"0\" width=\"50\" height=\"50\" fill=\"#ff0000\"/>"
      "<rect x=\"50\" y=\"0\" width=\"50\" height=\"50\" fill=\"url(#g)\"/></svg>";

static const char k_svg_evenodd[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
                                    "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#112233\" "
                                    "fill-rule=\"evenodd\"/></svg>";

// Inputs the GPU vector path rejects.
static const char k_svg_nofill[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
                                   "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"none\"/></svg>";

static const char k_svg_whitespace[] = "   \n\t  ";

static const char k_svg_garbage[] = "not an svg <<< &&& >>>";

// Multi-color with a per-shape opacity on the second shape: red at full opacity, then blue at 0.5.
static const char k_svg_color_opacity[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff0000\"/>"
      "<rect x=\"50\" y=\"50\" width=\"30\" height=\"30\" fill=\"#0000ff\" opacity=\"0.5\"/></svg>";

UTEST(vector_image, parse_solid_fill_monochrome) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img),
                    "a solid-fill SVG parses");
    const IntCase parsed[] = {
        { (int)img.shape_count, 2, "both rects become shapes" },
        { (int)img.classification, (int)FlVectorClass_Monochrome, "one resolved color -> monochrome" },
        { (int)img.mono_color.r, 255, "mono color R (#ff8000)" },
        { (int)img.mono_color.g, 128, "mono color G (#ff8000)" },
        { (int)img.mono_color.b, 0, "mono color B (#ff8000)" },
    };
    check_int_cases(utest_result, parsed, sizeof_array(parsed));
    const BoolCase size[] = {
        { img.width > 99.0f && img.width < 101.0f, true, "document width preserved" },
        { img.height > 79.0f && img.height < 81.0f, true, "document height preserved" },
    };
    check_bool_cases(utest_result, size, sizeof_array(size));
    for (u32 i = 0; i < img.shape_count; i++) {
        ASSERT_TRUE_MSG(img.shapes[i].segment_count > 0, "each shape has contour segments (never rasterized)");
        ASSERT_EQ_MSG((int)img.shapes[i].fill_rule, 0, "default fill rule is nonzero");
        ASSERT_TRUE_MSG(img.shapes[i].opacity > 0.99f, "default shape opacity is 1");
    }
}

UTEST(vector_image, classify_multicolor) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_multi, sizeof(k_svg_multi) - 1, s_arena, &img),
                    "a multi-color solid-fill SVG parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_MultiColor, ">=2 distinct colors -> multi-color");
}

UTEST(vector_image, reject_gradient) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_gradient, sizeof(k_svg_gradient) - 1, s_arena, &img),
                     "a gradient fill is unsupported -> rejected for the GPU path");
}

UTEST(vector_image, reject_stroke) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_stroke, sizeof(k_svg_stroke) - 1, s_arena, &img),
                     "a stroke is unsupported -> rejected for the GPU path");
}

UTEST(vector_image, reject_mixed_per_image_granularity) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_mixed, sizeof(k_svg_mixed) - 1, s_arena, &img),
                     "one unsupported shape in a multi-shape SVG rejects the whole image");
}

UTEST(vector_image, reject_zero_solid_shape) {
    // fill="none" with no stroke contributes no solid fill, so the image is rejected.
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_nofill, sizeof(k_svg_nofill) - 1, s_arena, &img),
                     "an SVG with no visible solid fill is rejected");
}

UTEST(vector_image, reject_empty) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)"", 0, s_arena, &img), "empty input rejects cleanly");
}

UTEST(vector_image, reject_whitespace) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_whitespace, sizeof(k_svg_whitespace) - 1, s_arena, &img),
                     "whitespace-only input rejects cleanly");
}

UTEST(vector_image, reject_garbage) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_garbage, sizeof(k_svg_garbage) - 1, s_arena, &img),
                     "garbage / non-SVG input rejects cleanly");
}

UTEST(vector_image, sw_path_decodes_what_gpu_rejects) {
    // A gradient SVG that the GPU vector path rejects still decodes to RGBA through the shared CPU path
    // (decode_svg).
    ImageDecodeOptions opts = { 0 };
    FlImageDecodeResult* result = image_decode(s_arena, k_svg_gradient, sizeof(k_svg_gradient) - 1, &opts);
    ASSERT_TRUE_MSG(result != nullptr, "SW decode_svg still rasterizes a gradient SVG");
    ASSERT_EQ_MSG((int)result->channels, 4, "SW path produces RGBA");
    ASSERT_TRUE_MSG(result->width > 0 && result->height > 0, "SW path produces non-empty pixels");
}

UTEST(vector_image, parse_evenodd_fill_rule) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_evenodd, sizeof(k_svg_evenodd) - 1, s_arena, &img),
                    "an evenodd solid-fill SVG parses");
    ASSERT_EQ_MSG((int)img.shape_count, 1, "one shape");
    ASSERT_EQ_MSG((int)img.shapes[0].fill_rule, 1, "fill-rule=evenodd maps to 1");
}

UTEST(vector_image, lower_segments_known_geometry) {
    // The vector path reuses the shared glyph lowering. A line lowers to one quadratic whose control point is the
    // segment midpoint, in y-down coordinates local to the padded cell: cell = (p.x - origin_x, origin_y - p.y).
    FlGlyphSegment line = { .kind = FlGlyphSegment_Line, .pts = { { 2.0f, 3.0f }, { 5.0f, 9.0f } } };
    f32 out[GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6];
    u32 quads = gpu_glyph_lower_segments(&line, 1, 1.0f, 10.0f, out);
    ASSERT_EQ_MSG((int)quads, 1, "a line yields one quadratic");
    const BoolCase points[] = {
        { out[0] == 1.0f && out[1] == 7.0f, true, "p0 = (2-1, 10-3)" },
        { out[2] == 2.5f && out[3] == 4.0f, true, "line control is the midpoint" },
        { out[4] == 4.0f && out[5] == 1.0f, true, "p1 = (5-1, 10-9)" },
    };
    check_bool_cases(utest_result, points, sizeof_array(points));
}

UTEST(vector_image, lower_cubic_known_geometry) {
    // A cubic with collinear, evenly spaced control points is a degree-elevated straight line on y=0. The shared
    // lowering splits it into four quadratics. All input y are 0, so every emitted point stays on y=0 after the
    // cell y-flip (origin_y - p.y = 0), and the chain spans x = 0..3.
    FlGlyphSegment cubic = {
        .kind = FlGlyphSegment_Cubic,
        .pts = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f }, { 3.0f, 0.0f } },
    };
    f32 out[GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6];
    u32 quads = gpu_glyph_lower_segments(&cubic, 1, 0.0f, 0.0f, out);
    ASSERT_EQ_MSG((int)quads, 4, "a cubic yields four quadratics");
    const BoolCase chain[] = {
        { out[0] == 0.0f && out[1] == 0.0f, true, "the chain starts at (0,0)" },
        { out[(4 - 1) * 6 + 4] == 3.0f && out[(4 - 1) * 6 + 5] == 0.0f, true, "the chain ends at (3,0)" },
    };
    check_bool_cases(utest_result, chain, sizeof_array(chain));
    for (u32 q = 0; q < quads; q++) {
        ASSERT_TRUE_MSG(out[q * 6 + 1] == 0.0f && out[q * 6 + 3] == 0.0f && out[q * 6 + 5] == 0.0f,
                        "every lowered point stays on the line y=0");
    }
}

UTEST(vector_image, lower_parsed_shape) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");
    const FlVectorShape* shape = &img.shapes[0];
    f32 out[512 * 6];
    u32 quads = gpu_glyph_lower_segments(shape->segments, shape->segment_count, 0.0f, img.height, out);
    ASSERT_TRUE_MSG(quads >= shape->segment_count, "every contour segment lowers to at least one quadratic");
}

// Color lowering: each shape lowers independently into a contiguous quad range, in document order, with its solid
// fill and opacity combined into one premultiplied RGBA8 word.
UTEST(vector_image, color_lower_grouping_and_bake) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_color_opacity, sizeof(k_svg_color_opacity) - 1, s_arena, &img),
                    "a multi-color SVG with opacity parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_MultiColor, "two distinct colors -> multi-color");
    ASSERT_EQ_MSG((int)img.shape_count, 2, "both rects become shapes");

    u32 total_segments = 0;
    for (u32 s = 0; s < img.shape_count; s++) {
        total_segments += img.shapes[s].segment_count;
    }
    f32* floats = arena_alloc_array(s_arena, f32, (size_t)total_segments * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    GpuColorShape shapes[2] = { 0 };
    u32 shape_count = 0;
    u32 total_quads = gpu_svg_color_lower(s_arena, &img, 1.0f, img.height, floats, shapes, &shape_count);

    ASSERT_EQ_MSG((int)shape_count, 2, "the shape count mirrors the document's shape count");

    // The quad ranges are contiguous and cover every lowered quad. The shader walks [quad_start, +quad_count).
    const IntCase ranges[] = {
        { (int)shapes[0].quad_start, 0, "the first shape starts at quad 0" },
        { (int)shapes[1].quad_start, (int)shapes[0].quad_count,
          "the second shape starts where the first ends (contiguous, no gap)" },
        { (int)(shapes[0].quad_count + shapes[1].quad_count), (int)total_quads,
          "the ranges cover exactly the total quad count" },
    };
    check_int_cases(utest_result, ranges, sizeof_array(ranges));
    ASSERT_TRUE_MSG(shapes[0].quad_count >= img.shapes[0].segment_count
                        && shapes[1].quad_count >= img.shapes[1].segment_count,
                    "each contour segment lowers to at least one quadratic");

    // The color is premultiplied RGBA8 with R in the low byte. Opacity is applied to the alpha and to the
    // premultiplied channels.
    const IntCase colors[] = {
        { (int)shapes[0].fill_rule, 0, "shape 0 fill rule is nonzero" },
        { (int)shapes[1].fill_rule, 0, "shape 1 fill rule is nonzero" },
        { (int)(shapes[0].color & 0xff), 255, "shape 0 (red) premultiplied R = 255" },
        { (int)((shapes[0].color >> 16) & 0xff), 0, "shape 0 (red) premultiplied B = 0" },
        { (int)((shapes[0].color >> 24) & 0xff), 255, "shape 0 (red) alpha = 255 (fully opaque)" },
        { (int)(shapes[1].color & 0xff), 0, "shape 1 (blue) premultiplied R = 0" },
        { (int)((shapes[1].color >> 16) & 0xff), 128, "shape 1 (blue) premultiplied B halves to 128" },
        { (int)((shapes[1].color >> 24) & 0xff), 128, "shape 1 (blue) alpha = 128 (opacity 0.5)" },
    };
    check_int_cases(utest_result, colors, sizeof_array(colors));
}

// The even-odd fill rule passes through the lowering unchanged, and the shader applies it per shape.
UTEST(vector_image, color_lower_evenodd_passthrough) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_evenodd, sizeof(k_svg_evenodd) - 1, s_arena, &img),
                    "an evenodd SVG parses");
    f32* floats = arena_alloc_array(s_arena, f32,
                                    (size_t)img.shapes[0].segment_count * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    GpuColorShape shape = { 0 };
    u32 shape_count = 0;
    u32 total_quads = gpu_svg_color_lower(s_arena, &img, 1.0f, img.height, floats, &shape, &shape_count);

    const IntCase lowered[] = {
        { (int)shape_count, 1, "one shape" },
        { (int)shape.fill_rule, 1, "the even-odd fill rule passes through to the wire record" },
        { (int)shape.quad_start, 0, "the only shape starts at quad 0" },
        { (int)shape.quad_count, (int)total_quads, "its range covers every lowered quad" },
        { (int)(shape.color & 0xff), 0x11, "premultiplied R = 0x11" },
        { (int)((shape.color >> 8) & 0xff), 0x22, "premultiplied G = 0x22" },
        { (int)((shape.color >> 16) & 0xff), 0x33, "premultiplied B = 0x33" },
        { (int)((shape.color >> 24) & 0xff), 0xff, "alpha = 0xff (opaque)" },
    };
    check_int_cases(utest_result, lowered, sizeof_array(lowered));
}

UTEST(vector_image, atlas_one_live_cell_per_handle) {
    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);

    const FlColor mono = { 255, 128, 0, 255 };
    const FlImageHandle h = 7;
    const GpuSvgEntry* a
        = gpu_svg_atlas_place(&atlas, &(GpuSvgPlaceRequest) { h, 32, 20, 24, FlVectorClass_Monochrome, mono, nullptr });
    ASSERT_TRUE_MSG(a != nullptr, "an icon places");
    const BoolCase placed[] = {
        { a->live, true, "the placed cell is live" },
        { a->mono_color.r == 255 && a->mono_color.g == 128 && a->mono_color.b == 0, true,
          "the cell records the authored fill (the mono draw default)" },
        { gpu_svg_atlas_find(&atlas, h) == a, true, "the handle resolves to its live cell" },
    };
    ASSERT_EQ_MSG((int)a->size, 32, "cell keyed at the requested size");
    ASSERT_EQ_MSG((int)a->cls, (int)FlVectorClass_Monochrome, "the cell records the classification");
    check_bool_cases(utest_result, placed, sizeof_array(placed));

    u32 count_before = atlas.entry_count;
    const GpuSvgEntry* a2
        = gpu_svg_atlas_place(&atlas, &(GpuSvgPlaceRequest) { h, 32, 20, 24, FlVectorClass_Monochrome, mono, nullptr });
    ASSERT_TRUE_MSG(a2 == a, "re-placing at the same size returns the same cell (idempotent)");
    ASSERT_EQ_MSG((int)atlas.entry_count, (int)count_before, "no new entry on an idempotent re-place");

    const GpuSvgEntry* b
        = gpu_svg_atlas_place(&atlas, &(GpuSvgPlaceRequest) { h, 64, 40, 48, FlVectorClass_Monochrome, mono, nullptr });
    ASSERT_TRUE_MSG(b != nullptr && b != a, "a size-step change appends a new cell");
    ASSERT_EQ_MSG((int)atlas.entry_count, (int)count_before + 1, "the new size adds exactly one entry");
    const BoolCase rekeyed[] = {
        { gpu_svg_atlas_find(&atlas, h) == b, true, "the handle now resolves to the new cell" },
        { a->live, false, "the prior cell is orphaned" },
    };
    check_bool_cases(utest_result, rekeyed, sizeof_array(rekeyed));

    const FlImageHandle g = 9;
    const GpuSvgEntry* c
        = gpu_svg_atlas_place(&atlas, &(GpuSvgPlaceRequest) { g, 32, 16, 16, FlVectorClass_Monochrome, mono, nullptr });
    ASSERT_TRUE_MSG(c != nullptr && gpu_svg_atlas_find(&atlas, g) == c, "a second handle places independently");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, h) == b, "the first handle is unaffected by the second");
}

UTEST(vector_image, svg_quantize_ladder) {
    const FlVectorClass mono = FlVectorClass_Monochrome;
    const IntCase steps[] = {
        { (int)svg_quantize_size(1, mono), 12, "below the ladder snaps to the first step" },
        { (int)svg_quantize_size(33, mono), 48, "33 snaps up to 48" },
        { (int)svg_quantize_size(64, mono), 64, "a step value maps to itself" },
        { (int)svg_quantize_size(9999, mono), 2048, "above the ladder clamps to the mono top step (2048)" },
    };
    check_int_cases(utest_result, steps, sizeof_array(steps));

    u16 prev = 0;
    for (u16 s = 1; s <= 2100; s++) {
        u16 q = svg_quantize_size(s, mono);
        ASSERT_EQ_MSG((int)svg_quantize_size(q, mono), (int)q, "quantize is idempotent at a step");
        ASSERT_TRUE_MSG(q >= prev, "quantize is monotonic non-decreasing");
        prev = q;
    }
}

// The per-class ceiling on the same ladder: a multi-color icon stops at 1024 (the RGBA8 atlas is 2048^2) and a
// monochrome icon at 2048 (the R8 atlas is 4096^2).
UTEST(vector_image, svg_quantize_per_class_clamp) {
    const FlVectorClass mono = FlVectorClass_Monochrome;
    const FlVectorClass color = FlVectorClass_MultiColor;

    // Below both caps the class makes no difference, since the ladder is shared. Just past the color cap, a color
    // icon clamps to 1024 and a mono icon moves to the next step (1536). 2048 is the top for mono and anything above
    // clamps there. Color stays at 1024.
    const IntCase ladder[] = {
        { (int)svg_quantize_size(500, mono), (int)svg_quantize_size(500, color),
          "below either cap the class does not change the step" },
        { (int)svg_quantize_size(1024, mono), 1024, "1024 is an exact step for both classes" },
        { (int)svg_quantize_size(1024, color), 1024, "1024 is the color cap, still an exact step" },
        { (int)svg_quantize_size(1025, color), 1024, "past the color cap clamps to 1024" },
        { (int)svg_quantize_size(1025, mono), 1536, "a mono icon keeps climbing past 1024" },
        { (int)svg_quantize_size(1536, mono), 1536, "1536 is a mono-only step" },
        { (int)svg_quantize_size(2048, mono), 2048, "2048 is the mono cap, an exact step" },
        { (int)svg_quantize_size(2049, mono), 2048, "past the mono cap clamps to 2048" },
        { (int)svg_quantize_size(9999, color), 1024, "a color icon never exceeds its 1024 cap" },
        { (int)svg_quantize_size(svg_quantize_size(9999, color), color), 1024, "color clamp is idempotent at the cap" },
        { (int)svg_quantize_size(svg_quantize_size(9999, mono), mono), 2048, "mono clamp is idempotent at the cap" },
    };
    check_int_cases(utest_result, ladder, sizeof_array(ladder));
}

// A host that zooms continuously quantizes its physical dst size onto the ladder to key the prepare call, then
// draws the cell at the exact dst. The larger, uniform side is the one quantized. A non-uniform stretch relies on
// linear sampling and does not raise the raster size for the short axis.
UTEST(vector_image, raster_size_for_dst) {
    const FlVectorClass mono = FlVectorClass_Monochrome;
    const IntCase dsts[] = {
        { (int)svg_raster_size_for_dst(40.0f, 30.0f, mono), 48,
          "the larger side (40) drives the ladder: snaps up to 48" },
        { (int)svg_raster_size_for_dst(30.0f, 40.0f, mono), 48, "axis order does not matter — uniform = max side" },
        { (int)svg_raster_size_for_dst(64.0f, 64.0f, mono), 64, "a square dst on a step maps to that step" },
        { (int)svg_raster_size_for_dst(32.1f, 10.0f, mono), 48,
          "a fractional size ceils before quantizing (33 -> 48)" },
        { (int)svg_raster_size_for_dst(0.0f, 0.0f, mono), 12, "a degenerate dst snaps to the smallest step" },
        // A non-uniform stretch: 96x40 and 96x90 quantize to the same raster size, so both use one cell and the
        // short axis is sampled, with no new raster.
        { (int)svg_raster_size_for_dst(96.0f, 40.0f, mono), (int)svg_raster_size_for_dst(96.0f, 90.0f, mono),
          "the short axis does not change the raster size — non-uniform stretch samples one cell" },
    };
    check_int_cases(utest_result, dsts, sizeof_array(dsts));

    // The size is continuous across the host's zoom while the raster size changes only at ladder steps. A sweep
    // of the uniform size over 17..24 stays on step 24, and 24 -> 25 is the only crossing into 32.
    for (f32 s = 17.0f; s <= 24.0f; s += 1.0f) {
        ASSERT_EQ_MSG((int)svg_raster_size_for_dst(s, s, mono), 24, "within a step, the raster size is constant");
    }
    const IntCase large[] = {
        { (int)svg_raster_size_for_dst(25.0f, 25.0f, mono), 32, "crossing a step boundary keys the next size" },
        // A mono dst beyond 512 keeps getting sharper: 800px keys step 1024.
        { (int)svg_raster_size_for_dst(800.0f, 600.0f, mono), 1024,
          "a >512px mono dst keys a larger-than-512 step (no longer capped at 512)" },
        { (int)svg_raster_size_for_dst(1600.0f, 1200.0f, mono), 2048,
          "a very large mono dst climbs to the 2048 mono cap" },
    };
    check_int_cases(utest_result, large, sizeof_array(large));
}

// The format records only what the GL layer will store. The CPU bookkeeping never branches on it, so an RGBA8
// atlas places, resolves, orphans and re-keys exactly as the R8 one does.
UTEST(vector_image, atlas_format_agnostic) {
    GpuSvgAtlas r8 = { 0 };
    GpuSvgAtlas rgba = { 0 };
    gpu_svg_atlas_init(&r8, s_arena, GpuSvgTexel_R8);
    gpu_svg_atlas_init(&rgba, s_arena, GpuSvgTexel_Rgba8);
    const IntCase formats[] = {
        { (int)r8.format, (int)GpuSvgTexel_R8, "the atlas records its format" },
        { (int)rgba.format, (int)GpuSvgTexel_Rgba8, "the rgba atlas records its format" },
    };
    check_int_cases(utest_result, formats, sizeof_array(formats));

    const FlColor color = { 10, 20, 30, 255 };
    const FlImageHandle h = 3;
    const GpuSvgEntry* a8
        = gpu_svg_atlas_place(&r8, &(GpuSvgPlaceRequest) { h, 32, 20, 24, FlVectorClass_MultiColor, color, nullptr });
    const GpuSvgEntry* ar
        = gpu_svg_atlas_place(&rgba, &(GpuSvgPlaceRequest) { h, 32, 20, 24, FlVectorClass_MultiColor, color, nullptr });
    ASSERT_TRUE_MSG(a8 != nullptr && ar != nullptr, "both formats place the cell");
    ASSERT_TRUE_MSG(a8->cell.x == ar->cell.x && a8->cell.y == ar->cell.y && a8->cell.w == ar->cell.w
                        && a8->cell.h == ar->cell.h,
                    "cell geometry is identical across formats (the packer is format-blind)");

    const GpuSvgEntry* b8
        = gpu_svg_atlas_place(&r8, &(GpuSvgPlaceRequest) { h, 64, 40, 48, FlVectorClass_MultiColor, color, nullptr });
    const GpuSvgEntry* br
        = gpu_svg_atlas_place(&rgba, &(GpuSvgPlaceRequest) { h, 64, 40, 48, FlVectorClass_MultiColor, color, nullptr });
    ASSERT_TRUE_MSG(b8 != nullptr && br != nullptr, "a step change appends a new cell in both");
    const BoolCase rekeyed[] = {
        { a8->live, false, "the R8 prior cell orphans" },
        { ar->live, false, "the RGBA8 prior cell orphans identically" },
        { gpu_svg_atlas_find(&r8, h) == b8 && gpu_svg_atlas_find(&rgba, h) == br, true,
          "both resolve the handle to the new live cell" },
        { r8.entry_count == rgba.entry_count, true, "identical entry counts after the same sequence" },
    };
    check_bool_cases(utest_result, rekeyed, sizeof_array(rekeyed));
}

// Compaction. The skyline packer only appends, so re-keying a handle onto new size steps leaves its earlier cells
// as orphans until a placement fails. gpu_svg_atlas_compact repacks only the live cells into an empty atlas,
// which drops every orphan and keeps each live handle at its current size and geometry. The bookkeeping moves
// cells and never texels, so R8 and RGBA8 compact to the same live set.

typedef struct CompactObs {
    bool overflowed;     // a placement failed because the atlas filled
    u32 orphans_before;  // orphaned cells present at overflow
    u32 count_before;    // entry count at overflow (live handles + orphans)
    u32 count_after;     // entry count after compaction (the live set)
    bool all_live_after; // every surviving entry is live
    bool a_resolved;     // the re-keyed handle A resolves after compaction...
    u16 a_size_after;    // ...at this size
    u16 a_last_placed;   // A's last successfully-placed (current) size
    bool b_resolved;     // the sibling B resolves after compaction...
    u16 b_size_after;    // ...at this size
    bool refit;          // a placement the full atlas refused now succeeds
    bool geometry_kept;  // the retained geometry pointers survived compaction
} CompactObs;
static u32 count_orphans(const GpuSvgAtlas* atlas) {
    u32 orphans = 0;
    for (u32 i = 0; i < atlas->entry_count; i++) {
        orphans += atlas->entries[i].live ? 0u : 1u;
    }
    return orphans;
}

// Re-keys A across distinct big sizes. Each step orphans A's previous cell, and several fit before the packer fills.
static void rekey_until_overflow(GpuSvgAtlas* atlas, const FlVectorImage* geomA, CompactObs* obs) {
    const FlColor mono = { 1, 2, 3, 255 };
    for (u16 s = 1; s <= 256; s++) {
        const GpuSvgEntry* e = gpu_svg_atlas_place(
            atlas, &(GpuSvgPlaceRequest) { 1, s, 360, 360, FlVectorClass_Monochrome, mono, geomA });
        if (e == nullptr) {
            obs->overflowed = true; // the failing placement leaves the prior live cell intact
            return;
        }
        obs->a_last_placed = s;
    }
}

static CompactObs run_compaction_sequence(GpuSvgAtlas* atlas, const FlVectorImage* geomA, const FlVectorImage* geomB) {
    const FlColor mono = { 1, 2, 3, 255 };
    const FlImageHandle A = 1, B = 2;
    CompactObs obs = { 0 };

    // A small sibling that has to survive compaction at its current size.
    gpu_svg_atlas_place(atlas, &(GpuSvgPlaceRequest) { B, 16, 16, 16, FlVectorClass_Monochrome, mono, geomB });

    rekey_until_overflow(atlas, geomA, &obs);
    obs.count_before = atlas->entry_count;
    obs.orphans_before = count_orphans(atlas);

    gpu_svg_atlas_compact(atlas);

    obs.count_after = atlas->entry_count;
    obs.all_live_after = count_orphans(atlas) == 0;
    const GpuSvgEntry* la = gpu_svg_atlas_find(atlas, A);
    const GpuSvgEntry* lb = gpu_svg_atlas_find(atlas, B);
    obs.a_resolved = la != nullptr;
    obs.a_size_after = la != nullptr ? la->size : 0;
    obs.b_resolved = lb != nullptr;
    obs.b_size_after = lb != nullptr ? lb->size : 0;
    obs.geometry_kept = la != nullptr && lb != nullptr && la->vector == geomA && lb->vector == geomB;

    // Space was reclaimed: a big placement that the full atlas refused now succeeds.
    const GpuSvgEntry* refit
        = gpu_svg_atlas_place(atlas, &(GpuSvgPlaceRequest) { 3, 7, 360, 360, FlVectorClass_Monochrome, mono, geomA });
    obs.refit = refit != nullptr;
    return obs;
}

static bool a_retained_at_last_size(const CompactObs* obs) {
    return obs->a_resolved && obs->a_size_after == obs->a_last_placed;
}

static bool compaction_fully_succeeded(const CompactObs* obs) {
    const bool handles_kept = obs->a_resolved && obs->b_resolved && obs->geometry_kept;
    return obs->overflowed && obs->all_live_after && obs->refit && handles_kept;
}

UTEST(vector_image, atlas_compaction) {
    FlVectorImage geomA = { 0 }, geomB = { 0 }; // distinct geometry sentinels that the bookkeeping never dereferences

    GpuSvgAtlas r8 = { 0 };
    gpu_svg_atlas_init(&r8, s_arena, GpuSvgTexel_R8);
    CompactObs r = run_compaction_sequence(&r8, &geomA, &geomB);

    const BoolCase r8_obs[] = {
        { r.overflowed, true, "re-keying a big cell across sizes overflows the append-only atlas" },
        { r.orphans_before > 0, true, "orphaned cells accumulate before compaction" },
        { r.count_before > 2, true, "the atlas holds the two live handles plus orphans before compaction" },
        { r.count_after == 2, true, "compaction keeps exactly the live cells (both handles, no orphans)" },
        { r.all_live_after, true, "every surviving cell is live after compaction" },
        { a_retained_at_last_size(&r), true, "the re-keyed handle is retained at its current size" },
        { r.b_resolved && r.b_size_after == 16, true, "the sibling handle is retained at its current size" },
        { r.geometry_kept, true, "compaction preserves each live cell's retained geometry pointer" },
        { r.refit, true, "compaction reclaims orphaned space for a placement the full atlas refused" },
    };
    check_bool_cases(utest_result, r8_obs, sizeof_array(r8_obs));

    // An RGBA8 atlas put through the same sequence compacts to the same live set.
    GpuSvgAtlas rgba = { 0 };
    gpu_svg_atlas_init(&rgba, s_arena, GpuSvgTexel_Rgba8);
    CompactObs g = run_compaction_sequence(&rgba, &geomA, &geomB);
    // The two atlases differ in extent (R8 4096^2, RGBA8 2048^2), so A reaches a different size before each
    // overflows. Each atlas keeps A at its own last placed size.
    const BoolCase rgba_obs[] = {
        { compaction_fully_succeeded(&g), true,
          "RGBA8 compaction behaves identically to R8 (format-blind bookkeeping)" },
        { g.count_after == r.count_after, true, "RGBA8 compacts to the same live-cell count as R8" },
        { a_retained_at_last_size(&g), true,
          "RGBA8 retains the re-keyed handle at its current size (its own overflow point)" },
    };
    check_bool_cases(utest_result, rgba_obs, sizeof_array(rgba_obs));
}

UTEST(vector_image, translate_routing) {
    GpuAtlasCell cell = { .x = 10, .y = 20, .w = 34, .h = 26 }; // full padded extent
    FlRect dst = { 100.0f, 200.0f, 48.0f, 48.0f };
    FlColor tint = { 12, 34, 56, 255 };

    FlCmdImage mono = gpu_svg_image_cmd(5, cell, FlVectorClass_Monochrome, tint, dst);
    const IntCase mono_ints[] = {
        { (int)mono.mode, FlImageMode_AlphaCoverage, "monochrome -> alpha-coverage mode" },
        { (int)mono.tint.r, 12, "monochrome carries the requested tint (recolor)" },
        { (int)mono.tint.g, 34, "monochrome tint G" },
        { (int)mono.source.kind, FlImageKind_Static, "static cached handle source" },
        { (int)mono.source.handle, 5, "the icon's image handle" },
    };
    check_int_cases(utest_result, mono_ints, sizeof_array(mono_ints));
    const BoolCase mono_src[] = {
        { mono.src.x == (f32)(10 + GPU_SVG_ATLAS_PAD), true, "src x insets the pad border" },
        { mono.src.y == (f32)(20 + GPU_SVG_ATLAS_PAD), true, "src y insets the pad border" },
        { mono.src.w == (f32)(34 - 2 * GPU_SVG_ATLAS_PAD), true, "src w drops both pads" },
        { mono.src.h == (f32)(26 - 2 * GPU_SVG_ATLAS_PAD), true, "src h drops both pads" },
        { mono.dst.x == 100.0f && mono.dst.w == 48.0f, true, "dst is the caller's rectangle" },
    };
    check_bool_cases(utest_result, mono_src, sizeof_array(mono_src));

    FlCmdImage color = gpu_svg_image_cmd(5, cell, FlVectorClass_MultiColor, tint, dst);
    const IntCase color_ints[] = {
        { (int)color.mode, FlImageMode_Color, "multi-color -> RGBA color mode" },
        { (int)color.tint.r, 255, "multi-color uses a white tint (no modulation)" },
        { (int)color.tint.g, 255, "white tint G" },
        { (int)color.tint.b, 255, "white tint B" },
        { (int)color.tint.a, 255, "white tint A" },
    };
    check_int_cases(utest_result, color_ints, sizeof_array(color_ints));
}
