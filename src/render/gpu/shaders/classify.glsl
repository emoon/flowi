// Pass 1: classification - one thread per TILE_SIZE-square tile, building each tile's command index list.
//
// Frame shape: classification then raster, both compute, into an offscreen RGBA8 target. This pass reads the
// draw commands and the pre-intersected AABBs and writes the per-tile index lists; it reads no clip rects.
// The tile-list layout is TILE_STRIDE u32 per tile - [count, overflow, index * TILE_CAPACITY] - where count
// is clamped to capacity and overflow counts the commands that did not fit.
//
// Signature seam (shader_compat_metal.h): GL declares the SSBOs and uniforms at global scope; MSL takes them as
// kernel arguments. The shared body below reads the same names on both.
#ifdef FL_METAL
kernel void classify_main(device const DrawCmd* cmds [[buffer(BIND_CLASSIFY_CMDS)]],
                          device const float4* aabbs [[buffer(BIND_CLASSIFY_AABBS)]],
                          device uint* tile_lists [[buffer(BIND_CLASSIFY_TILE_LISTS)]],
                          constant uint& cmd_count [[buffer(BIND_CLASSIFY_CMD_COUNT)]],
                          constant uint& tile_cols [[buffer(BIND_CLASSIFY_TILE_COLS)]],
                          constant uint& tile_rows [[buffer(BIND_CLASSIFY_TILE_ROWS)]],
                          uint3 _fl_global_id [[thread_position_in_grid]]) {
#else
LOCAL_SIZE_2D(TILE_SIZE, TILE_SIZE);
BUFFER(DrawCmds, DrawCmd, cmds, 0);
BUFFER(Aabbs, float4, aabbs, 1);
WRITE_BUFFER(TileLists, uint, tile_lists, 2);
UNIFORM(uint, cmd_count);
UNIFORM(uint, tile_cols);
UNIFORM(uint, tile_rows);
KERNEL(main) {
#endif
    uint2 tile = gl_GlobalInvocationID.xy; // one thread per 8x8 tile
    if (tile.x >= tile_cols || tile.y >= tile_rows) {
        return;
    }
    float2 tsize = float2(float(TILE_SIZE));
    float4 bounds = float4(float2(tile) * tsize, float2(tile) * tsize + tsize); // tile pixel rect: x0 y0 x1 y1
    float2 tcenter = (bounds.xy + bounds.zw) * 0.5;
    float thalf = length((bounds.zw - bounds.xy) * 0.5); // the tile's half-diagonal
    uint base = (tile.y * tile_cols + tile.x) * TILE_STRIDE;
    uint count = 0u;
    uint overflow = 0u;
    for (uint i = 0u; i < cmd_count; i++) {
        float4 bb = aabbs[i];
        if (bb.x < bounds.z && bb.z > bounds.x && bb.y < bounds.w && bb.w > bounds.y) {
            // Line-aware narrow phase for stroke segments: a tile inside the segment's AABB but far from
            // the line (a long diagonal's box over-covers) is rejected, so only tiles the capsule can reach
            // keep the command. The bound - segment distance to the tile center minus the tile half-diagonal -
            // is conservative and never drops a tile the capsule actually touches.
            if (cmds[i].kind == KIND_POLYLINE && (cmds[i].flags & POLY_STROKE) != 0u) {
                float4 seg = cmds[i].rect;
                float reach = cmds[i].radius.x + 0.5; // half-width plus the AA ramp
                if (segment_dist(tcenter, seg.xy, seg.zw) - thalf > reach) {
                    continue;
                }
            }
            if (count < TILE_CAPACITY) {
                tile_lists[base + TILE_HEADER + count] = i;
                count++;
            } else {
                overflow++;
            }
        }
    }
    tile_lists[base + 0u] = count;
    tile_lists[base + 1u] = overflow;
}
