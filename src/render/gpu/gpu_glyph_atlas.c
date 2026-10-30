#include "gpu_glyph_atlas.h"
#include <core/arena.h>
#include <core/log.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_glyph_atlas_init(GpuGlyphAtlas* atlas, struct FlArena* arena) {
    if (atlas->entries == NULL) {
        atlas->entries = arena_alloc_array(arena, GpuGlyphEntry, GPU_GLYPH_ATLAS_MAX_ENTRIES);
    }
    atlas->entry_count = 0;
    atlas->needs_wipe = false;
    gpu_atlas_pack_init(&atlas->pack, arena, GPU_GLYPH_ATLAS_SIZE, GPU_GLYPH_ATLAS_SIZE);
}

void gpu_glyph_atlas_on_overflow(GpuGlyphAtlas* atlas) {
    atlas->needs_wipe = true;
}

void gpu_glyph_atlas_wipe_if_needed(GpuGlyphAtlas* atlas, struct FlArena* arena) {
    if (atlas->needs_wipe) {
        gpu_glyph_atlas_init(atlas, arena); // clears needs_wipe
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static GpuGlyphEntry* find_entry(GpuGlyphAtlas* atlas, FlFontId font, u16 size, u32 glyph_id) {
    for (u32 i = 0; i < atlas->entry_count; i++) {
        GpuGlyphEntry* e = &atlas->entries[i];
        if (e->font == font && e->size == size && e->glyph_id == glyph_id) {
            return e;
        }
    }
    return NULL;
}

const GpuGlyphEntry* gpu_glyph_atlas_find(const GpuGlyphAtlas* atlas, FlFontId font, u16 size, u32 glyph_id) {
    return find_entry((GpuGlyphAtlas*)atlas, font, size, glyph_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const GpuGlyphEntry* gpu_glyph_atlas_place(GpuGlyphAtlas* atlas, FlFontId font, u16 size, u32 glyph_id, u32 w, u32 h,
                                           f32 left, f32 top, f32 advance) {
    if (atlas->entry_count >= GPU_GLYPH_ATLAS_MAX_ENTRIES) {
        log_error("gpu backend: glyph atlas entry table full (%d)", GPU_GLYPH_ATLAS_MAX_ENTRIES);
        return NULL;
    }
    if (find_entry(atlas, font, size, glyph_id) != NULL) {
        log_error("gpu backend: glyph (font %u, size %u, glyph %u) placed twice", (unsigned)font, (unsigned)size,
                  (unsigned)glyph_id);
        return NULL;
    }

    GpuAtlasCell cell = { 0, 0, w, h };
    if (w > 0 && h > 0 && !gpu_atlas_pack_insert(&atlas->pack, w, h, &cell.x, &cell.y)) {
        return NULL;
    }

    GpuGlyphEntry* entry = &atlas->entries[atlas->entry_count++];
    *entry = (GpuGlyphEntry) {
        .font = font,
        .size = size,
        .glyph_id = glyph_id,
        .cell = cell,
        .left = left,
        .top = top,
        .advance = advance,
    };
    return entry;
}
