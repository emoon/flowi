#include "gpu_image_atlas.h"
#include <core/arena.h>
#include <core/log.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_atlas_init(GpuImageAtlas* atlas, FlArena* arena) {
    atlas->entries = arena_alloc_array(arena, GpuImageAtlasEntry, GPU_IMAGE_ATLAS_MAX_ENTRIES);
    atlas->entry_count = 0;
    atlas->free_cells = arena_alloc_array(arena, GpuAtlasCell, GPU_IMAGE_ATLAS_MAX_ENTRIES);
    atlas->free_count = 0;
    atlas->pack = (GpuAtlasPack) { 0 };
    gpu_atlas_pack_init(&atlas->pack, arena, GPU_IMAGE_ATLAS_SIZE, GPU_IMAGE_ATLAS_SIZE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static GpuImageAtlasEntry* find_entry(GpuImageAtlas* atlas, FlImageHandle handle) {
    for (u32 i = 0; i < atlas->entry_count; i++) {
        if (atlas->entries[i].handle == handle) {
            return &atlas->entries[i];
        }
    }
    return NULL;
}

const GpuImageAtlasEntry* gpu_image_atlas_find(const GpuImageAtlas* atlas, FlImageHandle handle) {
    return find_entry((GpuImageAtlas*)atlas, handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const GpuImageAtlasEntry* gpu_image_atlas_place(GpuImageAtlas* atlas, FlImageHandle handle, u32 w, u32 h) {
    if (w == 0 || h == 0 || w > GPU_IMAGE_ATLAS_MAX_DIM || h > GPU_IMAGE_ATLAS_MAX_DIM) {
        return NULL;
    }
    if (atlas->entry_count >= GPU_IMAGE_ATLAS_MAX_ENTRIES) {
        log_error("gpu backend: image atlas entry table full (%d)", GPU_IMAGE_ATLAS_MAX_ENTRIES);
        return NULL;
    }
    if (find_entry(atlas, handle) != NULL) {
        log_error("gpu backend: image handle %u placed twice", (unsigned)handle);
        return NULL;
    }

    // First released cell the image fits in wins; else extend the skyline.
    GpuAtlasCell cell;
    bool placed = false;
    for (u32 i = 0; i < atlas->free_count; i++) {
        if (atlas->free_cells[i].w >= w && atlas->free_cells[i].h >= h) {
            cell = atlas->free_cells[i];
            atlas->free_cells[i] = atlas->free_cells[--atlas->free_count];
            placed = true;
            break;
        }
    }
    if (!placed) {
        cell.w = w;
        cell.h = h;
        if (!gpu_atlas_pack_insert(&atlas->pack, w, h, &cell.x, &cell.y)) {
            log_error("gpu backend: image atlas full, %ux%u image dropped", w, h);
            return NULL;
        }
    }

    GpuImageAtlasEntry* entry = &atlas->entries[atlas->entry_count++];
    *entry = (GpuImageAtlasEntry) { .handle = handle, .cell = cell, .img_w = w, .img_h = h };
    return entry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_atlas_release(GpuImageAtlas* atlas, FlImageHandle handle) {
    GpuImageAtlasEntry* entry = find_entry(atlas, handle);
    if (entry == NULL) {
        return;
    }
    if (atlas->free_count < GPU_IMAGE_ATLAS_MAX_ENTRIES) {
        atlas->free_cells[atlas->free_count++] = entry->cell;
    } else {
        log_warning("gpu backend: image atlas reuse list full; releasing handle %u leaks its cell", (unsigned)handle);
    }
    *entry = atlas->entries[--atlas->entry_count];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_atlas_expand_coverage(u8* dst, const u8* src, u32 w, u32 h, u32 src_stride) {
    u32 stride = src_stride != 0 ? src_stride : w;
    for (u32 y = 0; y < h; y++) {
        const u8* src_row = src + (size_t)y * (size_t)stride;
        u8* dst_row = dst + (size_t)y * (size_t)w * 4u;
        for (u32 x = 0; x < w; x++) {
            u8 a = src_row[x];
            dst_row[x * 4 + 0] = a;
            dst_row[x * 4 + 1] = a;
            dst_row[x * 4 + 2] = a;
            dst_row[x * 4 + 3] = a;
        }
    }
}
