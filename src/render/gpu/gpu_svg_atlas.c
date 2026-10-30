#include "gpu_svg_atlas.h"
#include <core/arena.h>
#include <core/log.h>

#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 gpu_svg_atlas_size(GpuSvgTexelFormat format) {
    return format == GpuSvgTexel_Rgba8 ? GPU_SVG_ATLAS_SIZE_COLOR : GPU_SVG_ATLAS_SIZE_MONO;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Geometric raster-size ladder: snap up to the smallest ladder step >= size, clamped to the top step. The
// steps grow ~1.4x-1.5x, and snap-up over a fixed table is monotonic and idempotent (any ladder value snaps to
// itself).

static const u16 k_svg_size_ladder[] = { 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048 };

// Per-class ladder ceiling: half the class's atlas extent. Both caps are exact ladder members.
static u16 svg_class_max_size(FlVectorClass cls) {
    GpuSvgTexelFormat format = cls == FlVectorClass_MultiColor ? GpuSvgTexel_Rgba8 : GpuSvgTexel_R8;
    return (u16)(gpu_svg_atlas_size(format) / 2u);
}

u16 svg_quantize_size(u16 size, FlVectorClass cls) {
    u16 cap = svg_class_max_size(cls);
    u16 top = k_svg_size_ladder[0]; // the largest step at or below the cap (the value sizes above the cap snap to)
    for (u32 i = 0; i < sizeof(k_svg_size_ladder) / sizeof(k_svg_size_ladder[0]); i++) {
        u16 step = k_svg_size_ladder[i];
        if (step > cap) {
            break; // never exceed the per-class cap, even if the ladder runs higher
        }
        if (size <= step) {
            return step;
        }
        top = step;
    }
    return top;
}

u16 svg_raster_size_for_dst(f32 dst_w, f32 dst_h, FlVectorClass cls) {
    // Uniform size = the larger destination side, in physical pixels, snapped onto the ladder: the prepare seam
    // fits the icon's larger document side to this raster size. Ceil so a fractional size never quantizes below
    // the pixels it will cover; non-positive sizes snap to the smallest step.
    f32 uniform = fmaxf(dst_w, dst_h);
    if (!(uniform > 0.0f)) {
        return svg_quantize_size(1, cls); // smallest step for a degenerate/zero dst (NaN-safe via negated compare)
    }
    f32 ceiled = ceilf(uniform);
    u16 px = ceiled >= 65535.0f ? 65535u : (u16)ceiled; // clamp into u16 before the ladder clamps to its top step
    return svg_quantize_size(px, cls);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_svg_atlas_init(GpuSvgAtlas* atlas, struct FlArena* arena, GpuSvgTexelFormat format) {
    if (atlas->entries == NULL) {
        atlas->entries = arena_alloc_array(arena, GpuSvgEntry, GPU_SVG_ATLAS_MAX_ENTRIES);
    }
    atlas->entry_count = 0;
    atlas->format = format;
    atlas->arena = arena;                    // retained so compaction can reset the packer without a fresh allocation
    u32 extent = gpu_svg_atlas_size(format); // per-format extent; the packer's node capacity is extent + 1
    gpu_atlas_pack_init(&atlas->pack, arena, extent, extent);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static GpuSvgEntry* find_live_entry(GpuSvgAtlas* atlas, FlImageHandle handle) {
    // At most one entry per handle is live, so the first match is the answer.
    for (u32 i = 0; i < atlas->entry_count; i++) {
        GpuSvgEntry* e = &atlas->entries[i];
        if (e->live && e->handle == handle) {
            return e;
        }
    }
    return NULL;
}

const GpuSvgEntry* gpu_svg_atlas_find(const GpuSvgAtlas* atlas, FlImageHandle handle) {
    return find_live_entry((GpuSvgAtlas*)atlas, handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const GpuSvgEntry* gpu_svg_atlas_place(GpuSvgAtlas* atlas, FlImageHandle handle, u16 size, u32 w, u32 h,
                                       FlVectorClass cls, FlColor mono_color, const FlVectorImage* vector) {
    GpuSvgEntry* live = find_live_entry(atlas, handle);
    if (live != NULL && live->size == size) {
        return live; // already current at this size - idempotent, no new placement
    }

    if (atlas->entry_count >= GPU_SVG_ATLAS_MAX_ENTRIES) {
        log_error("gpu backend: svg atlas entry table full (%d)", GPU_SVG_ATLAS_MAX_ENTRIES);
        return NULL;
    }

    GpuAtlasCell cell = { 0, 0, w, h };
    if (w > 0 && h > 0 && !gpu_atlas_pack_insert(&atlas->pack, w, h, &cell.x, &cell.y)) {
        // Pure rejecter: the backend answers a full atlas by compacting and retrying.
        return NULL;
    }

    // Orphan the handle's previous cell only after the new one is secured, so a failed placement leaves the
    // existing live cell intact.
    if (live != NULL) {
        live->live = false;
    }

    GpuSvgEntry* entry = &atlas->entries[atlas->entry_count++];
    *entry = (GpuSvgEntry) {
        .handle = handle,
        .size = size,
        .live = true,
        .cell = cell,
        .cls = cls,
        .mono_color = mono_color,
        .vector = vector,
    };
    return entry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_svg_atlas_compact(GpuSvgAtlas* atlas) {
    // Reset the packer to an empty atlas (reuses the existing node storage - same dimensions), then re-insert
    // only the live cells in their existing order. The write index can only trail the read index i (write
    // advances only on a live entry while i advances every step), so reading into a local copy before the write
    // makes the in-place compaction safe.
    u32 extent = gpu_svg_atlas_size(atlas->format);
    gpu_atlas_pack_init(&atlas->pack, atlas->arena, extent, extent);

    u32 write = 0;
    for (u32 i = 0; i < atlas->entry_count; i++) {
        GpuSvgEntry e = atlas->entries[i];
        if (!e.live) {
            continue;
        }

        GpuAtlasCell cell = { 0, 0, e.cell.w, e.cell.h };
        if (e.cell.w == 0 || e.cell.h == 0
            || !gpu_atlas_pack_insert(&atlas->pack, e.cell.w, e.cell.h, &cell.x, &cell.y)) {
            // A live cell that no longer fits the freshly-packed atlas is dropped; the icon re-prepares from its
            // retained geometry on a later frame.
            log_error("gpu backend: svg atlas compaction dropped a live cell that no longer fits");
            continue;
        }

        e.cell = cell;
        atlas->entries[write++] = e;
    }
    atlas->entry_count = write;
}
