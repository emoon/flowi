#include "gpu_atlas_pack.h"
#include <core/arena.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_atlas_pack_init(GpuAtlasPack* pack, FlArena* arena, u32 width, u32 height) {
    if (pack->nodes == NULL || pack->width != width) {
        pack->nodes = arena_alloc_array(arena, GpuAtlasPackNode, width + 1);
    }
    pack->width = width;
    pack->height = height;
    pack->nodes[0] = (GpuAtlasPackNode) { 0, 0 };
    pack->node_count = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The y a w-wide rect rests at with its left edge at nodes[first].x. False when it would overhang the right edge.

static bool fit_y_at(const GpuAtlasPack* pack, u32 first, u32 w, u32* out_y) {
    u32 x = pack->nodes[first].x;
    if (x + w > pack->width) {
        return false;
    }
    u32 y = 0;
    for (u32 i = first; i < pack->node_count && pack->nodes[i].x < x + w; i++) {
        if (pack->nodes[i].y > y) {
            y = pack->nodes[i].y;
        }
    }
    *out_y = y;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raise the skyline to y + h across [x, x + w). The height of the segment the span's right edge cuts into
// resumes at x + w.

static void add_rect(GpuAtlasPack* pack, u32 first, u32 x, u32 y, u32 w, u32 h) {
    u32 end = x + w;
    u32 resume_y = 0;
    u32 last = first;
    while (last < pack->node_count && pack->nodes[last].x < end) {
        resume_y = pack->nodes[last].y;
        last++;
    }
    bool need_resume = end < pack->width && (last >= pack->node_count || pack->nodes[last].x != end);
    u32 replaced = last - first;
    u32 inserted = 1u + (need_resume ? 1u : 0u);

    if (inserted > replaced) {
        // Capacity is width + 1, enough for one node per column.
        for (u32 i = pack->node_count; i > last; i--) {
            pack->nodes[i + inserted - replaced - 1] = pack->nodes[i - 1];
        }
    } else if (inserted < replaced) {
        for (u32 i = last; i < pack->node_count; i++) {
            pack->nodes[i - (replaced - inserted)] = pack->nodes[i];
        }
    }
    pack->node_count += inserted - replaced;

    pack->nodes[first] = (GpuAtlasPackNode) { x, y + h };
    if (need_resume) {
        pack->nodes[first + 1] = (GpuAtlasPackNode) { end, resume_y };
    }

    // Merge adjacent segments that ended up at equal height.
    u32 out = 0;
    for (u32 i = 1; i < pack->node_count; i++) {
        if (pack->nodes[i].y == pack->nodes[out].y) {
            continue;
        }
        out++;
        pack->nodes[out] = pack->nodes[i];
    }
    pack->node_count = out + 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool gpu_atlas_pack_insert(GpuAtlasPack* pack, u32 w, u32 h, u32* out_x, u32* out_y) {
    if (w == 0 || h == 0 || w > pack->width || h > pack->height) {
        return false;
    }

    // Bottom-left heuristic: lowest resulting top edge, ties broken by leftmost x - candidates are scanned
    // left to right, so the strict improvement test keeps the leftmost of equals.
    u32 best = UINT32_MAX;
    u32 best_x = 0;
    u32 best_y = 0;
    for (u32 i = 0; i < pack->node_count; i++) {
        u32 y;
        if (fit_y_at(pack, i, w, &y) && y + h <= pack->height && y + h < best) {
            best = y + h;
            best_x = pack->nodes[i].x;
            best_y = y;
        }
    }
    if (best == UINT32_MAX) {
        return false;
    }

    u32 first = 0;
    while (pack->nodes[first].x != best_x) {
        first++;
    }
    add_rect(pack, first, best_x, best_y, w, h);

    *out_x = best_x;
    *out_y = best_y;
    return true;
}
