// GPU smoke test: skyline packer, image-atlas bookkeeping, glyph atlas and glyph lowering unit tests.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Skyline packer and image-atlas bookkeeping unit tests (no GL; pure CPU placement logic).

typedef struct PackerCtx {
    FlArena* arena;
    GpuAtlasPack pack;
    u32 x;
    u32 y;
} PackerCtx;

static int check_packer_full_atlas(PackerCtx* c) {
    gpu_atlas_pack_init(&c->pack, c->arena, 64, 64);
    CHECK(gpu_atlas_pack_insert(&c->pack, 64, 64, &c->x, &c->y) && c->x == 0 && c->y == 0,
          "packer: an atlas-sized rect packs at the origin");
    CHECK(!gpu_atlas_pack_insert(&c->pack, 1, 1, &c->x, &c->y), "packer: a full atlas rejects further inserts");
    return 0;
}

static int check_packer_rejects(PackerCtx* c) {
    gpu_atlas_pack_init(&c->pack, c->arena, 64, 64);
    CHECK(!gpu_atlas_pack_insert(&c->pack, 0, 8, &c->x, &c->y), "packer: zero-width rects are rejected");
    CHECK(!gpu_atlas_pack_insert(&c->pack, 65, 8, &c->x, &c->y), "packer: rects wider than the atlas are rejected");
    return 0;
}

static int check_packer_skyline(PackerCtx* c) {
    CHECK(gpu_atlas_pack_insert(&c->pack, 32, 16, &c->x, &c->y) && c->x == 0 && c->y == 0,
          "packer: first rect lands bottom-left");
    CHECK(gpu_atlas_pack_insert(&c->pack, 32, 16, &c->x, &c->y) && c->x == 32 && c->y == 0,
          "packer: the bottom row fills before a new row starts");
    CHECK(gpu_atlas_pack_insert(&c->pack, 32, 16, &c->x, &c->y) && c->x == 0 && c->y == 16,
          "packer: the next row starts at the lowest available skyline");
    return 0;
}

static u32 next_random(u32* rng) {
    *rng ^= *rng << 13;
    *rng ^= *rng >> 17;
    *rng ^= *rng << 5;
    return *rng;
}

static int check_packer_sweep(PackerCtx* c) {
    gpu_atlas_pack_init(&c->pack, c->arena, 256, 256);
    GpuAtlasCell placed[128];
    u32 placed_count = 0;
    u32 rng = 0xC0FFEEu;
    for (u32 i = 0; i < 128; i++) {
        u32 r = next_random(&rng);
        u32 w = 1u + (r & 63u);
        u32 h = 1u + ((r >> 6) & 63u);
        if (!gpu_atlas_pack_insert(&c->pack, w, h, &c->x, &c->y)) {
            continue;
        }
        CHECK(c->x + w <= 256 && c->y + h <= 256, "packer sweep: every placement stays inside the atlas");
        GpuAtlasCell cell = { c->x, c->y, w, h };
        for (u32 j = 0; j < placed_count; j++) {
            CHECK(!cells_overlap(placed[j], cell), "packer sweep: placements must be pairwise disjoint");
        }
        placed[placed_count++] = cell;
    }
    CHECK(placed_count > 16, "packer sweep: a 256x256 atlas fits a healthy number of small rects");
    return 0;
}

static int check_image_atlas_placement(GpuImageAtlas* atlas) {
    const GpuImageAtlasEntry* a = gpu_image_atlas_place(atlas, 1, 64, 48);
    const GpuImageAtlasEntry* b = gpu_image_atlas_place(atlas, 2, 200, 16);
    CHECK(a != nullptr && b != nullptr, "atlas: two small images place");
    CHECK(a->img_w == 64 && a->img_h == 48, "atlas: the entry keeps the image extent");
    CHECK(!cells_overlap(a->cell, b->cell), "atlas: two images pack into disjoint cells");
    CHECK(a->cell.x + a->cell.w <= GPU_IMAGE_ATLAS_SIZE && a->cell.y + a->cell.h <= GPU_IMAGE_ATLAS_SIZE,
          "atlas: cells stay inside the texture");
    CHECK(gpu_image_atlas_find(atlas, 1) == a && gpu_image_atlas_find(atlas, 2) == b,
          "atlas: handles resolve to their entries");
    return 0;
}

static int check_image_atlas_rejects(GpuImageAtlas* atlas) {
    CHECK(gpu_image_atlas_find(atlas, 99) == nullptr, "atlas: an unknown handle does not resolve");
    CHECK(gpu_image_atlas_place(atlas, 3, GPU_IMAGE_ATLAS_MAX_DIM + 1, 8) == nullptr,
          "atlas: images past the per-image cap are rejected (the next slice's path)");
    CHECK(gpu_image_atlas_place(atlas, 1, 8, 8) == nullptr, "atlas: a handle cannot be placed twice");
    return 0;
}

static int check_image_atlas_release(GpuImageAtlas* atlas) {
    GpuAtlasCell freed = gpu_image_atlas_find(atlas, 1)->cell;
    gpu_image_atlas_release(atlas, 1);
    CHECK(gpu_image_atlas_find(atlas, 1) == nullptr, "atlas release: the handle stops resolving");
    gpu_image_atlas_release(atlas, 1); // releasing again is a no-op
    const GpuImageAtlasEntry* c = gpu_image_atlas_place(atlas, 4, 32, 32);
    CHECK(c != nullptr && c->cell.x == freed.x && c->cell.y == freed.y,
          "atlas release: the freed cell is reused by the next fitting placement");
    CHECK(c->cell.w == freed.w && c->cell.h == freed.h && c->img_w == 32 && c->img_h == 32,
          "atlas release: a reused cell keeps its full extent; the entry keeps the image extent");
    gpu_image_atlas_release(atlas, 4);
    const GpuImageAtlasEntry* d = gpu_image_atlas_place(atlas, 5, 64, 48);
    CHECK(d != nullptr && d->cell.x == freed.x && d->cell.y == freed.y,
          "atlas release: releasing a reused cell returns its full extent to the pool");
    return 0;
}

static int check_image_atlas(FlArena* arena) {
    GpuImageAtlas atlas;
    gpu_image_atlas_init(&atlas, arena);
    if (check_image_atlas_placement(&atlas) != 0) {
        return 1;
    }
    if (check_image_atlas_rejects(&atlas) != 0) {
        return 1;
    }
    return check_image_atlas_release(&atlas);
}

int run_packer_tests(void) {
    FlArena* arena = arena_new();
    PackerCtx ctx = { .arena = arena };
    if (check_packer_full_atlas(&ctx) != 0) {
        return 1;
    }
    if (check_packer_rejects(&ctx) != 0) {
        return 1;
    }
    if (check_packer_skyline(&ctx) != 0) {
        return 1;
    }
    if (check_packer_sweep(&ctx) != 0) {
        return 1;
    }
    if (check_image_atlas(arena) != 0) {
        return 1;
    }

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: packer/atlas unit tests PASS\n");
    return 0;
}

// Glyph-atlas bookkeeping unit tests (no GL; pure CPU keyed placement and lookup over the packer).

static int check_glyph_atlas_first_entry(GpuGlyphAtlas* atlas, const GpuGlyphEntry* a) {
    CHECK(a != nullptr, "glyph atlas: a glyph places");
    CHECK(a->cell.w == 20 && a->cell.h == 24, "glyph atlas: the cell keeps the padded extent");
    CHECK(a->cell.x + a->cell.w <= GPU_GLYPH_ATLAS_SIZE && a->cell.y + a->cell.h <= GPU_GLYPH_ATLAS_SIZE,
          "glyph atlas: cells stay inside the texture");
    CHECK(a->left == -1.0f && a->top == 23.0f && a->advance == 25.5f, "glyph atlas: metrics land verbatim");
    CHECK(gpu_glyph_atlas_find(atlas, 1, 32, 7) == a, "glyph atlas: the key resolves to its entry");
    return 0;
}

static int check_glyph_atlas_keys(GpuGlyphAtlas* atlas) {
    CHECK(gpu_glyph_atlas_find(atlas, 1, 32, 8) == nullptr, "glyph atlas: unknown glyph ids do not resolve");
    CHECK(gpu_glyph_atlas_find(atlas, 1, 34, 7) == nullptr, "glyph atlas: the size is part of the key");
    CHECK(gpu_glyph_atlas_find(atlas, 2, 32, 7) == nullptr, "glyph atlas: the font is part of the key");
    CHECK(gpu_glyph_atlas_place(atlas, (GpuGlyphKey) { 1, 32, 7 }, (GpuGlyphMetrics) { 20, 24, 0.0f, 0.0f, 0.0f })
              == nullptr,
          "glyph atlas: a key cannot be placed twice");
    return 0;
}

static int check_glyph_atlas_second_and_empty(GpuGlyphAtlas* atlas, const GpuGlyphEntry* a) {
    const GpuGlyphEntry* b
        = gpu_glyph_atlas_place(atlas, (GpuGlyphKey) { 1, 32, 8 }, (GpuGlyphMetrics) { 12, 30, 0.0f, 30.0f, 13.0f });
    CHECK(b != nullptr, "glyph atlas: a second glyph places");
    CHECK(!cells_overlap(a->cell, b->cell), "glyph atlas: two glyphs pack into disjoint cells");

    const GpuGlyphEntry* space
        = gpu_glyph_atlas_place(atlas, (GpuGlyphKey) { 1, 32, 9 }, (GpuGlyphMetrics) { 0, 0, 0.0f, 0.0f, 8.0f });
    CHECK(space != nullptr && space->cell.w == 0 && space->cell.h == 0,
          "glyph atlas: empty glyphs take a metrics-only entry");
    CHECK(gpu_glyph_atlas_find(atlas, 1, 32, 9) == space && space->advance == 8.0f,
          "glyph atlas: metrics-only entries resolve with their advance");
    return 0;
}

static int check_glyph_atlas_reinit(GpuGlyphAtlas* atlas, FlArena* arena) {
    gpu_glyph_atlas_init(atlas, arena);
    CHECK(gpu_glyph_atlas_find(atlas, 1, 32, 7) == nullptr, "glyph atlas: re-init forgets placements");
    const GpuGlyphEntry* c
        = gpu_glyph_atlas_place(atlas, (GpuGlyphKey) { 9, 16, 1 }, (GpuGlyphMetrics) { 8, 8, 0.0f, 8.0f, 8.0f });
    CHECK(c != nullptr && c->cell.x == 0 && c->cell.y == 0, "glyph atlas: re-init packs from the origin again");
    return 0;
}

int run_glyph_atlas_tests(void) {
    FlArena* arena = arena_new();

    GpuGlyphAtlas atlas = { 0 };
    gpu_glyph_atlas_init(&atlas, arena);

    const GpuGlyphEntry* a
        = gpu_glyph_atlas_place(&atlas, (GpuGlyphKey) { 1, 32, 7 }, (GpuGlyphMetrics) { 20, 24, -1.0f, 23.0f, 25.5f });
    if (check_glyph_atlas_first_entry(&atlas, a) != 0) {
        return 1;
    }
    if (check_glyph_atlas_keys(&atlas) != 0) {
        return 1;
    }
    if (check_glyph_atlas_second_and_empty(&atlas, a) != 0) {
        return 1;
    }
    if (check_glyph_atlas_reinit(&atlas, arena) != 0) {
        return 1;
    }

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: glyph-atlas unit tests PASS\n");
    return 0;
}

// Outline-lowering unit tests (no GL; gpu_glyph_lower.c is pure CPU).

// Evaluate one wire-format quadratic (6 floats: p0, control, p1) at t.
static Vec2 lowered_quad_eval(const f32* q, f32 t) {
    f32 s = 1.0f - t;
    return (Vec2) {
        s * s * q[0] + 2.0f * s * t * q[2] + t * t * q[4],
        s * s * q[1] + 2.0f * s * t * q[3] + t * t * q[5],
    };
}

static int check_lower_line(f32* floats, FlGlyphSegment* line) {
    // A line lowers to one degenerate quadratic (control = midpoint) in y-down cell coordinates: with the
    // cell origin at (1, 10), outline (2,3) -> (5,9) lands as (1,7) -> (4,1).
    *line = (FlGlyphSegment) { .kind = FlGlyphSegment_Line, .pts = { { 2.0f, 3.0f }, { 5.0f, 9.0f } } };
    FlGlyphOutline outline = { .segments = line, .segment_count = 1 };
    CHECK(gpu_glyph_lower_outline(&outline, 1.0f, 10.0f, floats) == 1, "lower: a line yields one quadratic");
    CHECK(floats[0] == 1.0f && floats[1] == 7.0f && floats[4] == 4.0f && floats[5] == 1.0f,
          "lower: endpoints map through the y-down cell transform");
    CHECK(floats[2] == 2.5f && floats[3] == 4.0f, "lower: a line's control is its midpoint");
    return 0;
}

static int check_lower_quadratic(f32* floats, FlGlyphSegment* quad) {
    *quad = (FlGlyphSegment) { .kind = FlGlyphSegment_Quadratic,
                               .pts = { { 0.0f, 0.0f }, { 4.0f, 8.0f }, { 8.0f, 0.0f } } };
    FlGlyphOutline outline = { .segments = quad, .segment_count = 1 };
    CHECK(gpu_glyph_lower_outline(&outline, 0.0f, 8.0f, floats) == 1, "lower: a quadratic yields one quadratic");
    CHECK(floats[0] == 0.0f && floats[1] == 8.0f && floats[2] == 4.0f && floats[3] == 0.0f && floats[4] == 8.0f
              && floats[5] == 8.0f,
          "lower: a quadratic's points pass through the transform verbatim");
    return 0;
}

static int check_lower_cubic_chain(const f32* floats) {
    CHECK(floats[0] == 0.0f && floats[1] == 8.0f && floats[3 * 6 + 4] == 8.0f && floats[3 * 6 + 5] == 8.0f,
          "lower: the chain spans the cubic's endpoints");
    for (int i = 0; i < 3; i++) {
        CHECK(floats[i * 6 + 4] == floats[(i + 1) * 6 + 0] && floats[i * 6 + 5] == floats[(i + 1) * 6 + 1],
              "lower: adjacent quadratics share their joint exactly");
    }
    return 0;
}

static int check_lower_cubic_pointwise(const f32* floats) {
    for (int i = 0; i < 4; i++) {
        for (int s = 0; s <= 4; s++) {
            f32 local_t = (f32)s * 0.25f;
            f32 global_t = ((f32)i + local_t) * 0.25f;
            // The source quadratic through the same y-down transform: x = 8t(1-t) + 8t^2 collapses to the
            // forms below with p0' = (0,8), c' = (4,0), p1' = (8,8).
            f32 gs = 1.0f - global_t;
            f32 rx = 2.0f * gs * global_t * 4.0f + global_t * global_t * 8.0f;
            f32 ry = 8.0f - 16.0f * gs * global_t;
            Vec2 pt = lowered_quad_eval(floats + i * 6, local_t);
            CHECK(fabsf(pt.x - rx) <= 0.001f && fabsf(pt.y - ry) <= 0.001f,
                  "lower: the elevated-quadratic cubic lowers exactly onto the source curve");
        }
    }
    return 0;
}

static int check_lower_cubic(f32* floats, FlGlyphSegment* cubic) {
    // This cubic is the degree-elevated form of the quadratic above (c1 = p0 + 2/3 (c - p0), c2 = p3 + 2/3
    // (c - p3)); elevation commutes with de Casteljau subdivision and the 3/4-rule control recovers an
    // elevated piece's quadratic exactly, so the lowered chain must reproduce the source curve pointwise.
    *cubic = (FlGlyphSegment) {
        .kind = FlGlyphSegment_Cubic,
        .pts = { { 0.0f, 0.0f }, { 8.0f / 3.0f, 16.0f / 3.0f }, { 16.0f / 3.0f, 16.0f / 3.0f }, { 8.0f, 0.0f } }
    };
    FlGlyphOutline outline = { .segments = cubic, .segment_count = 1 };
    CHECK(gpu_glyph_lower_outline(&outline, 0.0f, 8.0f, floats) == 4, "lower: a cubic yields four quadratics");
    if (check_lower_cubic_chain(floats) != 0) {
        return 1;
    }
    return check_lower_cubic_pointwise(floats);
}

int run_glyph_lower_tests(void) {
    f32 floats[4 * 6];
    FlGlyphSegment line;
    FlGlyphSegment quad;
    FlGlyphSegment cubic;
    if (check_lower_line(floats, &line) != 0) {
        return 1;
    }
    if (check_lower_quadratic(floats, &quad) != 0) {
        return 1;
    }
    if (check_lower_cubic(floats, &cubic) != 0) {
        return 1;
    }

    FlGlyphSegment mixed[3] = { line, quad, cubic };
    FlGlyphOutline outline = { .segments = mixed, .segment_count = 3 };
    f32 mixed_floats[3 * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6];
    CHECK(gpu_glyph_lower_outline(&outline, 0.0f, 8.0f, mixed_floats) == 6,
          "lower: a mixed outline lowers to the per-kind sum");

    printf("gpu_smoke_test: glyph-lowering unit tests PASS\n");
    return 0;
}

#endif // !defined(__APPLE__)
