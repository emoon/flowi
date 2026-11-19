#include "gpu_glyph_atlas.h"
#include <core/arena.h>
#include <core/log.h>

void gpu_glyph_atlas_init(GpuGlyphAtlas* atlas, struct FlArena* arena) {
    if (atlas->entries == nullptr) {
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

static bool entry_matches(const GpuGlyphEntry* e, GpuGlyphKey key) {
    if (e->font != key.font || e->size != key.size) {
        return false;
    }
    return e->glyph_index == key.glyph_index;
}

static GpuGlyphEntry* find_entry(GpuGlyphAtlas* atlas, GpuGlyphKey key) {
    for (u32 i = 0; i < atlas->entry_count; i++) {
        if (entry_matches(&atlas->entries[i], key)) {
            return &atlas->entries[i];
        }
    }
    return nullptr;
}

const GpuGlyphEntry* gpu_glyph_atlas_find(const GpuGlyphAtlas* atlas, FlFontId font, u16 size, u32 glyph_index) {
    return find_entry((GpuGlyphAtlas*)atlas, (GpuGlyphKey) { font, size, glyph_index });
}

// A zero-area glyph occupies no atlas space; otherwise the packer must have room.

static bool reserve_cell(GpuGlyphAtlas* atlas, GpuGlyphMetrics m, GpuAtlasCell* cell) {
    *cell = (GpuAtlasCell) { 0, 0, m.w, m.h };
    if (m.w == 0 || m.h == 0) {
        return true;
    }
    return gpu_atlas_pack_insert(&atlas->pack, m.w, m.h, &cell->x, &cell->y);
}

const GpuGlyphEntry* gpu_glyph_atlas_place(GpuGlyphAtlas* atlas, GpuGlyphKey key, GpuGlyphMetrics metrics) {
    if (atlas->entry_count >= GPU_GLYPH_ATLAS_MAX_ENTRIES) {
        log_error("gpu backend: glyph atlas entry table full (%d)", GPU_GLYPH_ATLAS_MAX_ENTRIES);
        return nullptr;
    }
    if (find_entry(atlas, key) != nullptr) {
        log_error("gpu backend: glyph (font %u, size %u, glyph %u) placed twice", (unsigned)key.font,
                  (unsigned)key.size, (unsigned)key.glyph_index);
        return nullptr;
    }

    GpuAtlasCell cell;
    if (!reserve_cell(atlas, metrics, &cell)) {
        return nullptr;
    }

    GpuGlyphEntry* entry = &atlas->entries[atlas->entry_count++];
    *entry = (GpuGlyphEntry) {
        .font = key.font,
        .size = key.size,
        .glyph_index = key.glyph_index,
        .cell = cell,
        .left = metrics.left,
        .top = metrics.top,
        .advance = metrics.advance,
    };
    return entry;
}
