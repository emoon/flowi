// Tile-list layout (gpu_debug.h), shared by the compute passes: TILE_SIZE-px square tiles, each owning a
// [count, overflow, index * TILE_CAPACITY] list. TILE_STRIDE is the per-tile u32 span; TILE_HEADER the two
// leading words (count, overflow) before the index list. TILE_SIZE and TILE_CAPACITY come from the generated
// #define preamble (the host constants).
#define TILE_HEADER 2u
#define TILE_STRIDE (TILE_HEADER + TILE_CAPACITY)
