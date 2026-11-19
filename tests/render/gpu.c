// gpu prepare, glyph atlas and orchestrator tests

#include "render/common.h"

// The backend-independent prepare core: the glyph cell extent, the SVG uniform fit, the mono contour scaling and
// the compaction retry that both the GL and Metal prepare paths use. These headless CPU cases are the only
// coverage this math gets on every platform, since gpu_resource_prepare.c (the GL half) is not compiled on macOS.

UTEST(gpu_prepare_core, glyph_cell_rounds_control_box_out_plus_pad) {
    // A control box of x [1.25, 6.5], y [-2.1, 9.75] rounds out to x [1, 7] and y [-3, 10], a 6 x 13 texel box,
    // plus the padding ring on every side.
    FlGlyphSegment seg = { .kind = FlGlyphSegment_Line, .pts = { { 2.0f, 0.0f }, { 5.0f, 8.0f } } };
    FlGlyphOutline outline = {
        .segments = &seg,
        .segment_count = 1,
        .advance = 7.0f,
        .min_x = 1.25f,
        .min_y = -2.1f,
        .max_x = 6.5f,
        .max_y = 9.75f,
    };

    GpuGlyphCell cell = gpu_prepare_glyph_cell(&outline);
    ASSERT_FALSE_MSG(cell.empty, "a glyph with contours and a real control box earns a cell");
    const IntCase extent[] = {
        { (int)cell.pw, 6 + 2 * GPU_GLYPH_ATLAS_PAD, "width = ceil(6.5) - floor(1.25), plus the ring" },
        { (int)cell.ph, 13 + 2 * GPU_GLYPH_ATLAS_PAD, "height = ceil(9.75) - floor(-2.1), plus the ring" },
    };
    check_int_cases(utest_result, extent, sizeof_array(extent));
    // The cell origin is its top-left corner in outline space. The origin is floored, and min is not translated
    // to 0, which keeps the autohinter's subpixel stem positions.
    const BoolCase origin[] = {
        { cell.left == 1.0f - (f32)GPU_GLYPH_ATLAS_PAD, true, "left = floor(min_x) - pad" },
        { cell.top == 10.0f + (f32)GPU_GLYPH_ATLAS_PAD, true, "top = ceil(max_y) + pad" },
    };
    check_bool_cases(utest_result, origin, sizeof_array(origin));
}

UTEST(gpu_prepare_core, glyph_cell_empty_glyph_is_metrics_only) {
    // A space has no contours and a zeroed box. Its advance is real, so the caller still places a metrics-only
    // entry, but there is no cell and no raster dispatch.
    FlGlyphOutline space = { .advance = 4.0f };
    GpuGlyphCell cell = gpu_prepare_glyph_cell(&space);
    ASSERT_TRUE_MSG(cell.empty, "an outline with no segments is empty");
    const IntCase carries[] = {
        { (int)cell.pw, 0, "an empty glyph carries no cell width" },
        { (int)cell.ph, 0, "an empty glyph carries no cell height" },
    };
    check_int_cases(utest_result, carries, sizeof_array(carries));

    // A glyph that has contours but whose control box rounds out to nothing is empty too. A zero-texel cell would
    // dispatch a zero-sized raster.
    FlGlyphSegment seg = { .kind = FlGlyphSegment_Line, .pts = { { 0.0f, 0.0f }, { 0.0f, 0.0f } } };
    FlGlyphOutline degenerate = { .segments = &seg, .segment_count = 1 };
    ASSERT_TRUE_MSG(gpu_prepare_glyph_cell(&degenerate).empty, "a zero-extent control box is empty");
}

UTEST(gpu_prepare_core, svg_fit_lands_the_larger_document_side_on_the_raster_size) {
    // A 100x80 document at raster size 32: the larger side (100) maps to 32, so scale = 0.32 and the content is
    // 32 x ceil(80 * 0.32) = 32 x 26. The fit is uniform, so the aspect ratio is kept.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");
    ASSERT_TRUE_MSG(img.width == 100.0f && img.height == 80.0f, "the document extent is 100x80");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 32, &fit), "a real document fits");
    ASSERT_TRUE_MSG(fit.scale == 32.0f / 100.0f, "the larger side scales onto the raster size");
    const IntCase content[] = {
        { (int)fit.pw, 32 + 2 * GPU_SVG_ATLAS_PAD, "content width = the raster size, plus the ring" },
        { (int)fit.ph, 26 + 2 * GPU_SVG_ATLAS_PAD, "content height = ceil(80 * 0.32) = 26, plus the ring" },
    };
    check_int_cases(utest_result, content, sizeof_array(content));

    // A document with no extent has nothing to place, and the fit rejects it before dividing by zero.
    FlVectorImage no_extent = img;
    no_extent.width = 0.0f;
    no_extent.height = 0.0f;
    ASSERT_FALSE_MSG(gpu_prepare_svg_fit(&no_extent, 32, &fit), "a zero-extent document does not fit");

    // A document with extent on one axis only fits to an empty cell and is also rejected, since a zero-texel cell
    // would dispatch a zero-sized raster.
    FlVectorImage no_width = img;
    no_width.width = 0.0f;
    FlVectorImage no_height = img;
    no_height.height = 0.0f;
    const BoolCase collapsed[] = {
        { gpu_prepare_svg_fit(&no_width, 32, &fit), false, "a fit that collapses the width to zero is rejected" },
        { gpu_prepare_svg_fit(&no_height, 32, &fit), false, "a fit that collapses the height to zero is rejected" },
    };
    check_bool_cases(utest_result, collapsed, sizeof_array(collapsed));
}

UTEST(gpu_prepare_core, svg_cell_geometry_rejects_a_cell_with_nothing_to_raster) {
    // A cell whose geometry is missing, or whose shapes carry no contours, lowers to nothing.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 32, &fit), "fits");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    const GpuSvgEntry* entry = gpu_svg_atlas_place(
        &atlas, &(GpuSvgPlaceRequest) { 1, 32, fit.pw, fit.ph, img.classification, img.mono_color, &img });
    ASSERT_TRUE_MSG(entry != nullptr, "the cell places");

    GpuSvgCellGeometry geom = gpu_prepare_svg_cell_geometry(s_arena, entry, nullptr, false);
    ASSERT_TRUE_MSG(geom.floats == nullptr, "a cell with no retained geometry lowers to nothing");

    FlVectorImage contourless = img;
    FlVectorShape empty_shapes[2] = { img.shapes[0], img.shapes[1] };
    empty_shapes[0].segment_count = 0;
    empty_shapes[1].segment_count = 0;
    contourless.shapes = empty_shapes;
    geom = gpu_prepare_svg_cell_geometry(s_arena, entry, &contourless, false);
    ASSERT_TRUE_MSG(geom.floats == nullptr, "shapes with no contours lower to nothing");
}

typedef struct PreparedExtent {
    f32 min_x, max_x, min_y, max_y;
} PreparedExtent;

static PreparedExtent lowered_extent(const GpuSvgCellGeometry* geom) {
    PreparedExtent e = {
        .min_x = geom->floats[0],
        .max_x = geom->floats[0],
        .min_y = geom->floats[1],
        .max_y = geom->floats[1],
    };
    for (u32 i = 0; i < geom->quad_count * 6; i += 2) {
        f32 x = geom->floats[i];
        f32 y = geom->floats[i + 1];
        e.min_x = x < e.min_x ? x : e.min_x;
        e.max_x = x > e.max_x ? x : e.max_x;
        e.min_y = y < e.min_y ? y : e.min_y;
        e.max_y = y > e.max_y ? y : e.max_y;
    }
    return e;
}

UTEST(gpu_prepare_core, svg_cell_geometry_lowers_mono_contours_into_the_padded_cell) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_Monochrome, "one shared fill -> monochrome");

    // Raster at the document's larger side, so the fit is 1:1 and the lowered coordinates are the document's own
    // apart from the y flip into the cell. The padding ring is then the only offset.
    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 100, &fit), "fits at 1:1");
    ASSERT_TRUE_MSG(fit.scale == 1.0f, "a raster size equal to the larger document side is a 1:1 fit");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    const GpuSvgEntry* entry = gpu_svg_atlas_place(
        &atlas, &(GpuSvgPlaceRequest) { 1, 100, fit.pw, fit.ph, img.classification, img.mono_color, &img });
    ASSERT_TRUE_MSG(entry != nullptr, "the cell places");

    GpuSvgCellGeometry geom = gpu_prepare_svg_cell_geometry(s_arena, entry, &img, false);
    ASSERT_TRUE_MSG(geom.floats != nullptr, "the mono cell lowers");
    ASSERT_TRUE_MSG(geom.shapes == nullptr && geom.shape_count == 0,
                    "the mono path lowers one undifferentiated soup — nothing per-shape to say");

    u32 total_segments = 0;
    for (u32 s = 0; s < img.shape_count; s++) {
        total_segments += img.shapes[s].segment_count;
    }
    ASSERT_TRUE_MSG(geom.quad_count >= total_segments,
                    "the soup carries every shape's contours — each segment lowers to at least one quadratic");

    // Two flips are involved. vector_image_parse turns y-down SVG into the y-up document space the shared
    // lowering expects (y_up = height - y_svg), and the lowering turns it back into the y-down cell
    // (cell_y = content_h + PAD - y_up). At 1:1 they cancel, so cell_y = y_svg + PAD, and cell_x is x_svg + PAD
    // since x is never flipped. k_svg_mono's two rects span SVG x [10, 70] and y [10, 60].
    PreparedExtent ext = lowered_extent(&geom);
    const f32 pad = (f32)GPU_SVG_ATLAS_PAD;
    const BoolCase extent[] = {
        { ext.min_x == 10.0f + pad, true, "the leftmost contour is SVG x=10, offset by the ring" },
        { ext.max_x == 70.0f + pad, true, "the rightmost contour is SVG x=70, offset by the ring" },
        { ext.min_y == 10.0f + pad, true, "the parse's y-up flip and the lowering's y-down flip cancel at 1:1" },
        { ext.max_y == 60.0f + pad, true, "so the bottom-most contour is SVG y=60, offset by the ring" },
    };
    check_bool_cases(utest_result, extent, sizeof_array(extent));
}

UTEST(gpu_prepare_core, svg_cell_geometry_scales_mono_contours_by_the_fit) {
    // The same geometry at half the raster size. Every contour coordinate scales by the fit before it is lowered,
    // so cell_x/cell_y become scale * svg + PAD.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 50, &fit), "fits at half the larger document side");
    ASSERT_TRUE_MSG(fit.scale == 0.5f, "the fit halves the document");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    const GpuSvgEntry* entry = gpu_svg_atlas_place(
        &atlas, &(GpuSvgPlaceRequest) { 1, 50, fit.pw, fit.ph, img.classification, img.mono_color, &img });
    ASSERT_TRUE_MSG(entry != nullptr, "the cell places");

    GpuSvgCellGeometry geom = gpu_prepare_svg_cell_geometry(s_arena, entry, &img, false);
    ASSERT_TRUE_MSG(geom.floats != nullptr, "the mono cell lowers");

    PreparedExtent ext = lowered_extent(&geom);
    const f32 pad = (f32)GPU_SVG_ATLAS_PAD;
    const BoolCase extent[] = {
        { ext.min_x == 5.0f + pad, true, "SVG x=10 scales to 5" },
        { ext.max_x == 35.0f + pad, true, "SVG x=70 scales to 35" },
        { ext.min_y == 5.0f + pad, true, "SVG y=10 scales to 5 (the two y flips still cancel)" },
        { ext.max_y == 30.0f + pad, true, "SVG y=60 scales to 30" },
    };
    check_bool_cases(utest_result, extent, sizeof_array(extent));
}

UTEST(gpu_prepare_core, svg_cell_geometry_keeps_shapes_distinct_on_the_color_path) {
    // The color path keeps its shapes separate, because the shader composites them in document order. The mono
    // path merges them.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_multi, sizeof(k_svg_multi) - 1, s_arena, &img), "parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_MultiColor, "distinct fills -> multi-color");
    ASSERT_EQ_MSG((int)img.shape_count, 2, "both rects become shapes");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 64, &fit), "fits");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_Rgba8);
    const GpuSvgEntry* entry = gpu_svg_atlas_place(
        &atlas, &(GpuSvgPlaceRequest) { 3, 64, fit.pw, fit.ph, img.classification, img.mono_color, &img });
    ASSERT_TRUE_MSG(entry != nullptr, "the cell places");

    GpuSvgCellGeometry geom = gpu_prepare_svg_cell_geometry(s_arena, entry, &img, true);
    ASSERT_TRUE_MSG(geom.floats != nullptr, "the color cell lowers");
    const IntCase wire[] = {
        { (int)geom.shape_count, (int)img.shape_count, "one wire record per document shape" },
        { (int)geom.shapes[0].quad_start, 0, "the first shape starts at quad 0" },
        { (int)(geom.shapes[0].quad_count + geom.shapes[1].quad_count), (int)geom.quad_count,
          "the per-shape ranges cover exactly the lowered quads" },
    };
    check_int_cases(utest_result, wire, sizeof_array(wire));
}

// The prepare core calls back into the backend only through a GpuRasterCellFn, the one step that differs between
// GL and Metal. A counting stub stands in for it.
typedef struct FakeRasterBackend {
    u32 raster_calls;
} FakeRasterBackend;

static void fake_raster_cell(void* backend, const GpuSvgEntry* entry, const FlVectorImage* vector, bool color) {
    (void)entry;
    (void)vector;
    (void)color;
    ((FakeRasterBackend*)backend)->raster_calls++;
}

UTEST(gpu_prepare_core, vector_into_rasters_once_per_size_step) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    FakeRasterBackend fake = { 0 };
    GpuSvgRaster raster = { .backend = &fake, .fn = fake_raster_cell };
    u32 compact_count = 0;

    compact_count += gpu_prepare_vector_into(&raster, &atlas, (GpuSvgKey) { .handle = 1, .size = 32 }, &img);
    ASSERT_EQ_MSG((int)fake.raster_calls, 1, "a fresh icon rasters once");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 1) != nullptr, "and it is placed live");

    compact_count += gpu_prepare_vector_into(&raster, &atlas, (GpuSvgKey) { .handle = 1, .size = 32 }, &img);
    ASSERT_EQ_MSG((int)fake.raster_calls, 1, "re-preparing at the same size does not re-dispatch");

    compact_count += gpu_prepare_vector_into(&raster, &atlas, (GpuSvgKey) { .handle = 1, .size = 64 }, &img);
    ASSERT_EQ_MSG((int)fake.raster_calls, 2, "a size step rasters into a fresh cell");
    ASSERT_EQ_MSG((int)compact_count, 0, "a roomy atlas never compacts");

    // A document with no extent is dropped before it reaches the atlas.
    FlVectorImage no_extent = img;
    no_extent.width = 0.0f;
    no_extent.height = 0.0f;
    compact_count += gpu_prepare_vector_into(&raster, &atlas, (GpuSvgKey) { .handle = 2, .size = 32 }, &no_extent);
    ASSERT_EQ_MSG((int)fake.raster_calls, 2, "a zero-extent document neither places nor rasters");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 2) == nullptr, "and nothing is live for it");
}

UTEST(gpu_prepare_core, vector_into_compacts_and_re_rasters_survivors_when_the_atlas_fills) {
    // Move one handle up the size ladder. Each step orphans the previous cell, because the packer only appends,
    // so the atlas eventually refuses a placement. The core then compacts the whole atlas, rasters every survivor
    // again from its retained geometry (the texels are still at their old positions) and retries the placement
    // once.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    FakeRasterBackend fake = { 0 };
    GpuSvgRaster raster = { .backend = &fake, .fn = fake_raster_cell };
    u32 compact_count = 0;

    // Near the top of the mono ladder each cell is roughly 2000x1600, so only a handful fit in the 4096^2 atlas.
    u32 steps = 0;
    for (u16 size = 2000; size < 2064 && compact_count == 0; size++) {
        compact_count += gpu_prepare_vector_into(&raster, &atlas, (GpuSvgKey) { .handle = 1, .size = size }, &img);
        steps++;
    }

    ASSERT_EQ_MSG((int)compact_count, 1, "the zoom fills the atlas and compacts exactly once");
    const BoolCase compacted[] = {
        { steps > 1, true, "it took several size steps to fill the atlas" },
        { fake.raster_calls > steps, true,
          "the compacting step rastered the surviving cell on top of the newly placed one" },
        { gpu_svg_atlas_find(&atlas, 1) != nullptr, true, "the retry placed the icon after the compaction" },
    };
    check_bool_cases(utest_result, compacted, sizeof_array(compacted));

    // The entry table went from steps cells to the one survivor. The retried placement then appended the new
    // cell and orphaned that survivor, so there is one live cell per handle.
    u32 live = 0;
    for (u32 i = 0; i < atlas.entry_count; i++) {
        live += atlas.entries[i].live ? 1u : 0u;
    }
    ASSERT_EQ_MSG((int)atlas.entry_count, 2, "the survivor plus the newly placed cell — every orphan is gone");
    ASSERT_EQ_MSG((int)live, 1, "the handle keeps exactly one live cell");
}

UTEST(gpu_prepare_core, vector_into_drops_an_icon_too_big_for_even_a_fresh_atlas) {
    // An icon whose cell cannot fit the atlas at any packing still fails the retried placement. It is dropped: it
    // is not rastered against a nullptr entry and not retried again.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    FakeRasterBackend fake = { 0 };
    GpuSvgRaster raster = { .backend = &fake, .fn = fake_raster_cell };
    u32 compact_count = 0;

    // The document's larger side is 100, so a raster size past the atlas extent gives a cell wider than the whole
    // 4096-texel atlas, which cannot be placed even after compaction.
    const u16 oversized = GPU_SVG_ATLAS_SIZE_MONO + 1000;
    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, oversized, &fit),
                    "the fit itself succeeds — it is the atlas that refuses");
    ASSERT_TRUE_MSG(fit.pw > GPU_SVG_ATLAS_SIZE_MONO, "the cell really is wider than the atlas");

    compact_count += gpu_prepare_vector_into(&raster, &atlas, (GpuSvgKey) { .handle = 1, .size = oversized }, &img);

    ASSERT_EQ_MSG((int)fake.raster_calls, 0, "an unplaceable icon is never rastered");
    ASSERT_EQ_MSG((int)compact_count, 1, "the atlas was compacted once in the attempt to make room");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 1) == nullptr, "and the icon is dropped, not left half-placed");
}

// An overflow partway through a frame's prepare pass does not wipe the atlas at once. Glyphs already placed this
// frame have to keep resolving, or they would be blank at draw. Overflow sets a deferred wipe that the backend
// carries out at the end of render(), and the next frame repacks into an empty atlas.
UTEST(gpu_glyph_atlas, earlier_glyph_survives_same_frame_overflow) {
    GpuGlyphAtlas atlas = { 0 };
    gpu_glyph_atlas_init(&atlas, s_arena);

    const FlFontId font = 1;
    const u16 size = 16;

    const GpuGlyphEntry* early = gpu_glyph_atlas_place(&atlas, (GpuGlyphKey) { font, size, 'A' },
                                                       (GpuGlyphMetrics) { 8, 8, 0.0f, 0.0f, 8.0f });
    ASSERT_TRUE_MSG(early != nullptr, "the early glyph places into the fresh atlas");

    // Keep placing oversized cells until the skyline packer refuses.
    bool overflowed = false;
    for (u32 i = 0; i < GPU_GLYPH_ATLAS_MAX_ENTRIES && !overflowed; i++) {
        const GpuGlyphEntry* e = gpu_glyph_atlas_place(&atlas, (GpuGlyphKey) { font, size, 1000 + i },
                                                       (GpuGlyphMetrics) { 256, 256, 0.0f, 0.0f, 0.0f });
        if (e == nullptr) {
            gpu_glyph_atlas_on_overflow(&atlas); // what prepare_glyph does on overflow
            overflowed = true;
        }
    }
    ASSERT_TRUE_MSG(overflowed, "the oversized cells exhaust the atlas");
    ASSERT_TRUE_MSG(atlas.needs_wipe, "overflow flags the deferred wipe");

    ASSERT_TRUE_MSG(gpu_glyph_atlas_find(&atlas, font, size, 'A') != nullptr,
                    "the early glyph survives a same-frame overflow");

    // End of render(): carry out the deferred wipe.
    gpu_glyph_atlas_wipe_if_needed(&atlas, s_arena);
    ASSERT_FALSE_MSG(atlas.needs_wipe, "honoring the wipe clears the flag");
    ASSERT_TRUE_MSG(gpu_glyph_atlas_find(&atlas, font, size, 'A') == nullptr,
                    "after the frame-boundary wipe the atlas is empty");

    // A frame with no overflow sets nothing, so the call does nothing and a populated atlas is kept.
    const GpuGlyphEntry* fresh = gpu_glyph_atlas_place(&atlas, (GpuGlyphKey) { font, size, 'B' },
                                                       (GpuGlyphMetrics) { 8, 8, 0.0f, 0.0f, 8.0f });
    ASSERT_TRUE_MSG(fresh != nullptr, "a glyph places into the wiped atlas");
    ASSERT_FALSE_MSG(atlas.needs_wipe, "a normal placement does not flag a wipe");
    gpu_glyph_atlas_wipe_if_needed(&atlas, s_arena);
    ASSERT_TRUE_MSG(gpu_glyph_atlas_find(&atlas, font, size, 'B') != nullptr,
                    "an unflagged honor call is a no-op and keeps the live set intact");
}

// The backend-independent per-frame sequence and the shared text-glyph walk, run on the CPU through a fake
// GpuRenderOps that records the order of the op calls and every prepare_glyph call. This is the only coverage the
// shared orchestrator gets in the GL/Linux build, whose render path never runs in CI. On Metal the end-to-end
// gpu_smoke_test runs the same code.

typedef enum {
    ORCH_FRAME_BEGIN = 1,
    ORCH_TRANSLATE,
    ORCH_BEGIN_ENCODER,
    ORCH_UPLOAD_INPUTS,
    ORCH_CLASSIFY,
    ORCH_BARRIER,
    ORCH_RASTER,
    ORCH_END_ENCODER,
} OrchOpId;

typedef struct OrchProbe {
    struct FlFontCache* font_cache; // returned by get_font_cache. With nullptr the walk returns early
    GpuDrawList translate_result;   // what translate returns. Its .count reaches classify as cmd_count

    OrchOpId ops_seen[16];
    u32 op_count;
    u32 classify_cmd_count;

    struct {
        FlFontId font;
        u16 size;
        u32 glyph_index;
    } glyphs[16];
    u32 glyph_count;
} OrchProbe;

static void orch_log(OrchProbe* p, OrchOpId id) {
    if (p->op_count < 16) {
        p->ops_seen[p->op_count++] = id;
    }
}

static void orch_frame_begin(void* b) {
    orch_log((OrchProbe*)b, ORCH_FRAME_BEGIN);
}

static struct FlFontCache* orch_get_font_cache(void* b) {
    return ((OrchProbe*)b)->font_cache;
}

static void orch_prepare_glyph(void* b, FlFontId font, u16 size, u32 glyph_index,
                               const struct FlGlyphOutline* outline) {
    OrchProbe* p = b;
    (void)outline;
    if (p->glyph_count < 16) {
        p->glyphs[p->glyph_count].font = font;
        p->glyphs[p->glyph_count].size = size;
        p->glyphs[p->glyph_count].glyph_index = glyph_index;
        p->glyph_count++;
    }
}

static GpuDrawList orch_translate(void* b, const struct FlRenderFrame* frame, u32 w, u32 h) {
    (void)frame;
    (void)w;
    (void)h;
    orch_log((OrchProbe*)b, ORCH_TRANSLATE);
    return ((OrchProbe*)b)->translate_result;
}

static void orch_begin_encoder(void* b, void* surface) {
    (void)surface;
    orch_log((OrchProbe*)b, ORCH_BEGIN_ENCODER);
}

static void orch_upload_inputs(void* b, const GpuDrawList* list) {
    (void)list;
    orch_log((OrchProbe*)b, ORCH_UPLOAD_INPUTS);
}

static void orch_classify(void* b, void* surface, u32 cmd_count) {
    (void)surface;
    ((OrchProbe*)b)->classify_cmd_count = cmd_count;
    orch_log((OrchProbe*)b, ORCH_CLASSIFY);
}

static void orch_barrier(void* b) {
    orch_log((OrchProbe*)b, ORCH_BARRIER);
}

static void orch_raster(void* b, void* surface) {
    (void)surface;
    orch_log((OrchProbe*)b, ORCH_RASTER);
}

static void orch_end_encoder(void* b) {
    orch_log((OrchProbe*)b, ORCH_END_ENCODER);
}

static const GpuRenderOps k_orch_probe_ops = {
    .frame_begin = orch_frame_begin,
    .get_font_cache = orch_get_font_cache,
    .prepare_glyph = orch_prepare_glyph,
    .translate = orch_translate,
    .begin_encoder = orch_begin_encoder,
    .upload_inputs = orch_upload_inputs,
    .classify = orch_classify,
    .barrier = orch_barrier,
    .raster = orch_raster,
    .end_encoder = orch_end_encoder,
};

typedef struct OrchTextRun {
    FlFontId font;
    u16 size;
    const u32* glyph_indices;
    u32 count;
} OrchTextRun;

static FlRenderFrame orch_make_text_frame(u8* buf, const OrchTextRun* text_run) {
    const FlFontId font = text_run->font;
    const u16 size = text_run->size;
    const u32* glyph_indices = text_run->glyph_indices;
    const u32 n = text_run->count;
    FlCmdHeader* hdr = (FlCmdHeader*)buf;
    FlCmdTextRun* run = (FlCmdTextRun*)(hdr + 1);
    FlGlyph* glyphs = (FlGlyph*)(run + 1);
    hdr->kind = FlCmd_TextRun;
    hdr->flags = 0;
    hdr->byte_size = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdTextRun) + (size_t)n * sizeof(FlGlyph));
    run->font = font;
    run->size = size;
    run->glyph_count = n;
    run->scale = 1.0f;
    for (u32 i = 0; i < n; i++) {
        glyphs[i] = (FlGlyph) { .glyph_index = glyph_indices[i] };
    }
    FlRenderFrame frame = { 0 };
    frame.commands = buf;
    frame.bytes = hdr->byte_size;
    return frame;
}

UTEST(gpu_orchestrator, canonical_sequence_and_null_font_cache_short_circuit) {
    GpuGlyphAtlas glyph_atlas = { 0 };
    gpu_glyph_atlas_init(&glyph_atlas, s_arena);
    FlArena* frame_arena = arena_new();

    OrchProbe probe = { 0 };
    probe.font_cache = nullptr; // with no cache the walk realizes no glyph
    probe.translate_result = (GpuDrawList) { .count = 7, .clip_count = 1 };

    u8 buf[256];
    const u32 glyph_indices[] = { 1, 2 };
    FlRenderFrame frame = orch_make_text_frame(buf, &(OrchTextRun) { 1, 32, glyph_indices, 2 });

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, nullptr, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);

    static const OrchOpId expected[] = { ORCH_FRAME_BEGIN, ORCH_TRANSLATE, ORCH_BEGIN_ENCODER, ORCH_UPLOAD_INPUTS,
                                         ORCH_CLASSIFY,    ORCH_BARRIER,   ORCH_RASTER,        ORCH_END_ENCODER };
    ASSERT_EQ_MSG((int)probe.op_count, 8, "every op runs exactly once");
    for (u32 i = 0; i < probe.op_count; i++) {
        ASSERT_EQ_MSG((int)probe.ops_seen[i], (int)expected[i], "ops run in the canonical order");
    }
    ASSERT_EQ_MSG((int)probe.classify_cmd_count, 7, "translate's list.count reaches classify as cmd_count");
    ASSERT_EQ_MSG((int)probe.glyph_count, 0, "a nullptr font cache short-circuits the glyph walk");

    fl_arena_destroy(frame_arena);
}

UTEST(gpu_orchestrator, text_run_realizes_each_glyph_through_op) {
    GpuGlyphAtlas glyph_atlas = { 0 };
    gpu_glyph_atlas_init(&glyph_atlas, s_arena);
    FlArena* frame_arena = arena_new();

    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;
    const u16 size_q4 = font_cache_whole_px_to_q4(size); // text runs carry the quarter-px size key
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");

    OrchProbe probe = { 0 };
    probe.font_cache = cache;
    probe.translate_result = (GpuDrawList) { 0 };

    u8 buf[256];
    const u32 glyph_indices[] = {
        font_cache_get_glyph_index(cache, font, 'A'),
        font_cache_get_glyph_index(cache, font, 'o'),
    };
    ASSERT_TRUE_MSG(glyph_indices[0] != 0 && glyph_indices[1] != 0, "the test font maps 'A' and 'o'");
    FlRenderFrame frame = orch_make_text_frame(buf, &(OrchTextRun) { font, size_q4, glyph_indices, 2 });

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, nullptr, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);

    ASSERT_EQ_MSG((int)probe.glyph_count, 2, "both mapped glyphs realize through the prepare_glyph op");
    ASSERT_EQ_MSG((int)probe.glyphs[0].glyph_index, (int)glyph_indices[0], "first glyph keeps its glyph index key");
    ASSERT_EQ_MSG((int)probe.glyphs[1].glyph_index, (int)glyph_indices[1], "second glyph keeps its glyph index key");
    for (u32 i = 0; i < probe.glyph_count; i++) {
        ASSERT_EQ_MSG((int)probe.glyphs[i].font, (int)font, "the run's font id is passed through");
        ASSERT_EQ_MSG((int)probe.glyphs[i].size, (int)size_q4, "the run's raster size key is passed through");
    }

    fl_arena_destroy(frame_arena);
}

UTEST(gpu_orchestrator, fallback_face_run_is_keyed_by_its_own_font_and_index) {
    GpuGlyphAtlas glyph_atlas = { 0 };
    gpu_glyph_atlas_init(&glyph_atlas, s_arena);
    FlArena* frame_arena = arena_new();

    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size_q4 = font_cache_whole_px_to_q4(32);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID && fallback != FONT_ID_INVALID, "the embedded test fonts load");
    font_cache_set_fallback_font(cache, fallback);

    OrchProbe probe = { 0 };
    probe.font_cache = cache;

    u8 buf[256];
    const u32 glyph_indices[] = { font_cache_get_glyph_index(cache, fallback, 'B') };
    ASSERT_TRUE_MSG(glyph_indices[0] != 0, "the fallback font maps 'B'");
    FlRenderFrame frame = orch_make_text_frame(buf, &(OrchTextRun) { fallback, size_q4, glyph_indices, 1 });

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, nullptr, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);

    ASSERT_EQ_MSG((int)probe.glyph_count, 1, "the fallback-face run realizes its glyph");
    ASSERT_EQ_MSG((int)probe.glyphs[0].font, (int)fallback, "the key is the run's font");
    ASSERT_EQ_MSG((int)probe.glyphs[0].glyph_index, (int)glyph_indices[0], "the key is the run's glyph index");

    fl_arena_destroy(frame_arena);
    font_cache_destroy(cache);
}

UTEST(gpu_orchestrator, builtin_font_run_draws_fallback_outlines) {
    GpuGlyphAtlas glyph_atlas = { 0 };
    gpu_glyph_atlas_init(&glyph_atlas, s_arena);
    FlArena* frame_arena = arena_new();

    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size_q4 = font_cache_whole_px_to_q4(32);
    FlFontId builtin = font_cache_load_default_font(cache, 1);
    ASSERT_TRUE_MSG(builtin != FONT_ID_INVALID, "the built-in font loads");

    OrchProbe probe = { 0 };
    probe.font_cache = cache;

    u8 buf[256];
    const u32 glyph_indices[] = { 'B' };
    FlRenderFrame frame = orch_make_text_frame(buf, &(OrchTextRun) { builtin, size_q4, glyph_indices, 1 });

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, nullptr, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);
    ASSERT_EQ_MSG((int)probe.glyph_count, 0, "with no fallback font a built-in run realizes nothing");

    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    ASSERT_TRUE_MSG(fallback != FONT_ID_INVALID, "the fallback font loads");
    font_cache_set_fallback_font(cache, fallback);

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, nullptr, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);
    ASSERT_EQ_MSG((int)probe.glyph_count, 1, "the fallback face supplies the outline");
    ASSERT_EQ_MSG((int)probe.glyphs[0].font, (int)builtin, "the key keeps the run's font");
    ASSERT_EQ_MSG((int)probe.glyphs[0].glyph_index, (int)'B', "the key keeps the run's glyph index");

    fl_arena_destroy(frame_arena);
    font_cache_destroy(cache);
}
