#include "gpu_atlas_pack.h"
#include <core/arena.h>

void gpu_atlas_pack_init(GpuAtlasPack* pack, FlArena* arena, u32 width, u32 height) {
    if (pack->nodes == nullptr || pack->width != width) {
        pack->nodes = arena_alloc_array(arena, GpuAtlasPackNode, width + 1);
    }
    pack->width = width;
    pack->height = height;
    pack->nodes[0] = (GpuAtlasPackNode) { 0, 0 };
    pack->node_count = 1;
}

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

// Open `by` slots at `last` by moving the tail up. Capacity is width + 1, enough for one node per column.

static void shift_tail_up(GpuAtlasPack* pack, u32 last, u32 by) {
    for (u32 i = pack->node_count; i > last; i--) {
        pack->nodes[i + by - 1] = pack->nodes[i - 1];
    }
}

static void shift_tail_down(GpuAtlasPack* pack, u32 last, u32 by) {
    for (u32 i = last; i < pack->node_count; i++) {
        pack->nodes[i - by] = pack->nodes[i];
    }
}

// Make the node list hold `inserted` nodes where `replaced` nodes ended at `last`.

static void resize_node_span(GpuAtlasPack* pack, u32 last, u32 replaced, u32 inserted) {
    if (inserted > replaced) {
        shift_tail_up(pack, last, inserted - replaced);
    } else {
        shift_tail_down(pack, last, replaced - inserted);
    }
    pack->node_count += inserted - replaced;
}

// Merge adjacent segments that sit at equal height.

static void merge_equal_segments(GpuAtlasPack* pack) {
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

// Raise the skyline to rect.y + rect.h across [rect.x, rect.x + rect.w). The height of the segment the span's
// right edge cuts into resumes at rect.x + rect.w.

static void add_rect(GpuAtlasPack* pack, u32 first, GpuAtlasCell rect) {
    u32 end = rect.x + rect.w;
    u32 resume_y = 0;
    u32 last = first;
    while (last < pack->node_count && pack->nodes[last].x < end) {
        resume_y = pack->nodes[last].y;
        last++;
    }
    bool need_resume = end < pack->width && (last >= pack->node_count || pack->nodes[last].x != end);
    u32 replaced = last - first;
    u32 inserted = need_resume ? 2u : 1u;

    resize_node_span(pack, last, replaced, inserted);

    pack->nodes[first] = (GpuAtlasPackNode) { rect.x, rect.y + rect.h };
    if (need_resume) {
        pack->nodes[first + 1] = (GpuAtlasPackNode) { end, resume_y };
    }
    merge_equal_segments(pack);
}

static bool rect_fits_atlas(const GpuAtlasPack* pack, u32 w, u32 h) {
    if (w == 0 || h == 0) {
        return false;
    }
    return w <= pack->width && h <= pack->height;
}

static bool candidate_beats(const GpuAtlasPack* pack, u32 y, u32 h, u32 best) {
    return y + h <= pack->height && y + h < best;
}

// Bottom-left heuristic: lowest resulting top edge, ties broken by leftmost x. Candidates are scanned
// left to right, so the strict improvement test keeps the leftmost of equals. Returns the node index or
// node_count when nothing fits.

static u32 find_best_node(const GpuAtlasPack* pack, u32 w, u32 h, u32* out_y) {
    u32 best = UINT32_MAX;
    u32 best_node = pack->node_count;
    for (u32 i = 0; i < pack->node_count; i++) {
        u32 y;
        if (fit_y_at(pack, i, w, &y) && candidate_beats(pack, y, h, best)) {
            best = y + h;
            best_node = i;
            *out_y = y;
        }
    }
    return best_node;
}

bool gpu_atlas_pack_insert(GpuAtlasPack* pack, u32 w, u32 h, u32* out_x, u32* out_y) {
    if (!rect_fits_atlas(pack, w, h)) {
        return false;
    }

    u32 best_y = 0;
    u32 first = find_best_node(pack, w, h, &best_y);
    if (first == pack->node_count) {
        return false;
    }

    u32 best_x = pack->nodes[first].x;
    add_rect(pack, first, (GpuAtlasCell) { best_x, best_y, w, h });

    *out_x = best_x;
    *out_y = best_y;
    return true;
}
