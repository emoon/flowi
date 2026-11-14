#include "software_renderer.h"
#include "sr_background.h"
#include "sr_command.h"
#include "sr_image_backend.h"
#include "sr_ir_to_render_command.h"
#include "sr_render_image_scale.h"
#include "software_renderer_internal.h"

#include <core/arena.h>
#include <core/log.h>
#include <core/math.h>
#include <core/os/os.h>
#include <core/perf_scope.h>
#include <core/simd.h>
#include "external/clay/clay.h"
#include <ui/image/image_cache.h>
#include <ui/image/image_decode.h>
#include <ui/render_backend.h>
#include <assert.h>
#include <math.h>
#include <string.h>

static int get_tile_size(int pos, int max_size, int tile_size) {
    if ((pos + tile_size) > max_size) {
        return max_size - pos;
    } else {
        return tile_size;
    }
}

static bool create_tile_setup(SwSurface* surface, int width, int height) {
    FlArena* arena = surface->arena;

    size_t rgb24_size = 0;
    if (!image_checked_pixel_size(width, height, 3, &rgb24_size)) {
        log_error("Software surface %dx%d is too large", width, height);
        return false;
    }

    // Reset to the initial position; everything below is set up again
    fl_arena_rewind(arena);

    const int tile_cols = (width + TILE_WIDTH - 1) / TILE_WIDTH;
    const int tile_rows = (height + TILE_HEIGHT - 1) / TILE_HEIGHT;
    int max_count = tile_cols * tile_rows;
    int tile_count = 0;
    f32x4* tiles_aabb = arena_alloc_array(arena, f32x4, max_count);

    for (int y = 0; y < height; y += TILE_HEIGHT) {
        for (int x = 0; x < width; x += TILE_WIDTH) {
            int tile_width = get_tile_size(x, width, TILE_WIDTH);
            int tile_height = get_tile_size(y, height, TILE_HEIGHT);
            tiles_aabb[tile_count++] = f32x4_new((f32)x, (f32)y, (f32)(x + tile_width), (f32)(y + tile_height));
        }
    }

    surface->tiles_aabb = tiles_aabb;
    surface->rgb24_output_buffer = arena_alloc_array(arena, u8, rgb24_size);
    // tile_buffer is resolution-independent and allocated once in create_software_renderer (tile_arena).

    surface->tiles_info = arena_alloc_array_zero(arena, Tile, tile_count);
    surface->tiles_info_count = tile_count;

    surface->screen_width = width;
    surface->screen_height = height;
    surface->tile_cols = tile_cols;
    surface->tile_rows = tile_rows;
    return true;
}

// Surface lifecycle: a SW surface owns its output pixel buffer + tile grid in a dedicated arena.

static FlSurface* sw_create_surface(void* user_data, int width, int height) {
    (void)user_data;
    FlArena* arena = arena_new();
    SwSurface* surface = arena_alloc_zero(arena, SwSurface);
    surface->arena = arena;
    // create_tile_setup rewinds this arena; the pinned floor keeps the SwSurface header intact.
    arena_pin_rewind_floor(arena);
    if (!create_tile_setup(surface, width, height)) {
        fl_arena_destroy(arena);
        return nullptr;
    }
    return (FlSurface*)surface;
}

static void sw_destroy_surface(void* user_data, FlSurface* surface) {
    (void)user_data;
    SwSurface* surf = (SwSurface*)surface;
    if (surf != nullptr) {
        fl_arena_destroy(surf->arena);
    }
}

static void change_resolution(void* user_data, FlSurface* surface, int width, int height) {
    (void)user_data;
    (void)create_tile_setup((SwSurface*)surface, width, height); // an oversized request keeps the old setup
}

// No-op: glyphs are rasterized on demand at the requested size.
static void scale_changed(void* user_data, float ui_scale) {
    (void)user_data;
    (void)ui_scale;
}

// Scalar tail covering the rightmost width & 15 columns the 16-px SIMD loop skips, one pixel at a time.
static NO_INLINE void copy_tile_tail_to_output(u8* output, int output_pitch, const i16* tile_data, TileRect rect) {
    const int first_tail = rect.width & ~15; // == (width >> 4) * 16

    for (int j = 0; j < rect.height; j++) {
        const int r_plane_row_start = R_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int g_plane_row_start = G_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int b_plane_row_start = B_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int output_row_start = (rect.y0 + j) * output_pitch + rect.x0 * 3;
        for (int k = first_tail; k < rect.width; k++) {
            u8* out_px = output + output_row_start + k * 3;
            out_px[0] = narrow_shr7_u8(tile_data[r_plane_row_start + k]);
            out_px[1] = narrow_shr7_u8(tile_data[g_plane_row_start + k]);
            out_px[2] = narrow_shr7_u8(tile_data[b_plane_row_start + k]);
        }
    }
}

// Copy tile data from planar RGB format to interleaved RGB output buffer
static void copy_tile_to_output(u8* output, const i16* tile_data, const f32x4 tile_aabb, int output_pitch) {
    const TileRect rect = tile_rect_from_aabb(tile_aabb);

    for (int j = 0; j < rect.height; j++) {
        const int r_plane_row_start = R_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int g_plane_row_start = G_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int b_plane_row_start = B_PLANE_START_OFFSET + j * TILE_STRIDE;

        const int output_row_start = (rect.y0 + j) * output_pitch + rect.x0 * 3;

        const int vector_chunks = rect.width >> 4;
        for (int k = 0; k < vector_chunks; k++) {
            const int chunk_offset = k * 16;

            i16x8x2 r_chunk_16 = i16x8x2_load(tile_data, r_plane_row_start + chunk_offset);
            i16x8x2 g_chunk_16 = i16x8x2_load(tile_data, g_plane_row_start + chunk_offset);
            i16x8x2 b_chunk_16 = i16x8x2_load(tile_data, b_plane_row_start + chunk_offset);

            u8x16 r_chunk = i16x8x2_pack_to_u8x16_narrow_shr(r_chunk_16, 7);
            u8x16 g_chunk = i16x8x2_pack_to_u8x16_narrow_shr(g_chunk_16, 7);
            u8x16 b_chunk = i16x8x2_pack_to_u8x16_narrow_shr(b_chunk_16, 7);

            const int output_chunk_offset = output_row_start + chunk_offset * 3;
            u8x16_store_interleaved_3(output + output_chunk_offset, r_chunk, g_chunk, b_chunk, 0);
        }
    }

    // Non-16-aligned tile widths leave a rightmost tail the SIMD loop skips.
    if ((rect.width & 15) != 0) {
        copy_tile_tail_to_output(output, output_pitch, tile_data, rect);
    }
}

static void dispatch_command(RenderData* self, const RenderCommand* render_command, const TileInfo* tile_info,
                             f32x4 scissor_rect) {
    u32 render_flags = render_command->mask;

    if (render_flags & RenderMask_Text) {
        sr_render_text(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_TextureSameSize) {
        sr_render_image_nearest(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_TextureScaled) {
        sr_render_image_scale(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_Border) {
        sr_render_border(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_DrawQuad) {
        sr_render_fill(self->tile_buffer, render_command, scissor_rect, tile_info);
    }
}

// Tiles with only clip-rect commands can use the direct-to-output fast path

static bool tile_has_visual_commands(const Tile* tile, const RenderCommand* commands) {
    for (int t = 0, count = tile->render_ids_count; t < count; t++) {
        u32 flags = commands[tile->render_ids[t]].mask;
        if (!(flags & (RenderMask_ClipRectBegin | RenderMask_ClipRectEnd))) {
            return true;
        }
    }
    return false;
}

#define MAX_SCISSOR_STACK_DEPTH 16

// Nested clipping regions in tile-relative coordinates; the bottom entry is the whole tile.
typedef struct ScissorStack {
    f32x4 rects[MAX_SCISSOR_STACK_DEPTH];
    int depth;
    // Pushes dropped at capacity; each is matched against a later pop before the real stack unwinds.
    int ignored_pushes;
} ScissorStack;

static ScissorStack scissor_stack_new(void) {
    ScissorStack stack = { .depth = 1 };
    stack.rects[0] = f32x4_new(0.0f, 0.0f, (f32)TILE_WIDTH, (f32)TILE_HEIGHT);
    return stack;
}

static f32x4 scissor_stack_top(const ScissorStack* stack) {
    return stack->rects[stack->depth - 1];
}

static f32x4 intersect_rects(f32x4 a, f32x4 b) {
    return f32x4_new(fmaxf(f32x4_extract(a, 0), f32x4_extract(b, 0)), fmaxf(f32x4_extract(a, 1), f32x4_extract(b, 1)),
                     fminf(f32x4_extract(a, 2), f32x4_extract(b, 2)), fminf(f32x4_extract(a, 3), f32x4_extract(b, 3)));
}

static void scissor_stack_push(ScissorStack* stack, f32x4 tile_relative_clip) {
    if (stack->depth >= MAX_SCISSOR_STACK_DEPTH) {
        // At capacity: count the dropped push so its matching pop skips the real stack.
        stack->ignored_pushes++;
        return;
    }
    stack->rects[stack->depth] = intersect_rects(tile_relative_clip, scissor_stack_top(stack));
    stack->depth++;
}

static void scissor_stack_pop(ScissorStack* stack) {
    if (stack->ignored_pushes > 0) {
        stack->ignored_pushes--;
    } else if (stack->depth > 1) {
        stack->depth--;
    }
}

static void render_tile_commands(RenderData* self, const Tile* tile, f32x4 tile_aabb) {
    const TileInfo tile_info = {
        .offsets = f32x4_shuffle(tile_aabb, tile_aabb, 0, 1, 0, 1),
    };
    ScissorStack scissor = scissor_stack_new();

    for (int t = 0, id_count = tile->render_ids_count; t < id_count; t++) {
        const RenderCommand* render_command = &self->render_commands[tile->render_ids[t]];
        u32 render_flags = render_command->mask;

        if (render_flags & RenderMask_ClipRectBegin) {
            scissor_stack_push(&scissor, f32x4_sub(render_command->aabb, tile_info.offsets));
        } else if (render_flags & RenderMask_ClipRectEnd) {
            scissor_stack_pop(&scissor);
        } else {
            dispatch_command(self, render_command, &tile_info, scissor_stack_top(&scissor));
        }
    }
}

static void render_tiles(RenderData* self, SwSurface* surface) {
    perf_scope_function();

    const SrBackground background
        = sr_background_resolve(&self->background, surface->screen_width, surface->screen_height);

    // The SW backend renders into its own offscreen RGB24 buffer; the host presents it.
    u8* output = surface->rgb24_output_buffer;
    int pitch = surface->screen_width * 3;

    for (int i = 0, count = surface->tiles_info_count; i < count; ++i) {
        const Tile* tile = &surface->tiles_info[i];
        const f32x4 tile_aabb = surface->tiles_aabb[i];

        // Fast path: tiles with no visual render commands bypass the planar tile buffer
        // and write directly to output, saving ~60KB memory traffic per tile
        if (!tile_has_visual_commands(tile, self->render_commands)
            && sr_background_write_direct(&background, output, pitch, tile_aabb)) {
            continue;
        }

        sr_background_fill_tile(&background, self->tile_buffer, tile_aabb);
        render_tile_commands(self, tile, tile_aabb);
        copy_tile_to_output(output, self->tile_buffer, tile_aabb, pitch);
    }
}

static bool is_clip_command(const RenderCommand* cmd) {
    return (cmd->mask & RenderMask_ClipRectBegin) || (cmd->mask & RenderMask_ClipRectEnd);
}

static void append_render_id(Tile* tile, int command_index) {
    tile->render_ids[tile->render_ids_count++] = (u32)command_index;
}

// Clip commands affect the entire screen, so they go to every tile
static void bin_clip_to_all_tiles(SwSurface* surface, int command_index) {
    for (int t = 0; t < surface->tiles_info_count; t++) {
        append_render_id(&surface->tiles_info[t], command_index);
    }
}

static void bin_to_covered_tiles(SwSurface* surface, const RenderCommand* cmd, int command_index) {
    const int cols = surface->tile_cols;
    const i32x4 max_tiles = i32x4_new(cols - 1, surface->tile_rows - 1, cols - 1, surface->tile_rows - 1);
    const TileRange range = tile_range_from_aabb(cmd->aabb, max_tiles);

    for (int ty = range.ty0; ty <= range.ty1; ty++) {
        for (int tx = range.tx0; tx <= range.tx1; tx++) {
            append_render_id(&surface->tiles_info[ty * cols + tx], command_index);
        }
    }
}

// Reset tile render_ids; the previous frame's pointers are invalid after fl_arena_rewind.
static void clear_tile_render_ids(SwSurface* surface) {
    for (int t = 0; t < surface->tiles_info_count; t++) {
        surface->tiles_info[t].render_ids = nullptr;
        surface->tiles_info[t].render_ids_count = 0;
    }
}

// Single contiguous allocation for all tiles' render_ids (rewound each frame)
static void assign_tile_render_ids(SwSurface* surface, FlArena* frame_arena, int cmd_count) {
    u32* all_render_ids = arena_alloc_array(frame_arena, u32, (u64)surface->tiles_info_count * cmd_count);

    for (int t = 0; t < surface->tiles_info_count; t++) {
        surface->tiles_info[t].render_ids = all_render_ids + (u64)t * cmd_count;
        surface->tiles_info[t].render_ids_count = 0;
    }
}

static void bin_tiles(RenderData* self, SwSurface* surface, FlArena* frame_arena,
                      UiRenderCommandsArray render_commands) {
    perf_scope_function();
    (void)self;

#ifdef FLOWI_UI_INSPECTOR
    self->stat_bin_alloc_bytes = 0;
    self->stat_clip_broadcasts = 0;
#endif

    if (surface->tiles_info_count == 0) {
        return;
    }

    if (render_commands.count == 0) {
        clear_tile_render_ids(surface);
        return;
    }

#ifdef FLOWI_UI_INSPECTOR
    void* bin_alloc_before = fl_arena_current_pos(frame_arena);
#endif

    assign_tile_render_ids(surface, frame_arena, render_commands.count);

#ifdef FLOWI_UI_INSPECTOR
    // Bytes the arena consumed, alignment padding included.
    self->stat_bin_alloc_bytes = (u32)((u8*)fl_arena_current_pos(frame_arena) - (u8*)bin_alloc_before);
#endif

    for (int i = 0; i < render_commands.count; i++) {
        const RenderCommand* cmd = &render_commands.commands[i];

        if (is_clip_command(cmd)) {
            bin_clip_to_all_tiles(surface, i);
#ifdef FLOWI_UI_INSPECTOR
            self->stat_clip_broadcasts += (u32)surface->tiles_info_count;
#endif
        } else {
            bin_to_covered_tiles(surface, cmd, i);
        }
    }
}

// The previous handle crosses only while a crossfade is active, so a valid prev is the sole crossfade signal.
// crossfade_t round-trips through 0..1 losslessly for every 0..255 progress value.
static BackgroundState background_from_frame(const FlBackground* bg) {
    return (BackgroundState) {
        .image_handle = (ImageHandle)bg->current,
        .prev_image_handle = (ImageHandle)bg->prev,
        .alpha = bg->alpha,
        .crossfade_t = (u8)(bg->crossfade_t * 255.0f + 0.5f),
        .crossfade_active = bg->prev != FL_IMAGE_HANDLE_INVALID,
        .clear_r = bg->clear.r,
        .clear_g = bg->clear.g,
        .clear_b = bg->clear.b,
    };
}

// Build the SW glyph cache on first use, once the font cache has been set.
static SwGlyphCache* ensure_glyph_cache(RenderData* self) {
    if (self->glyph_cache == nullptr && self->font_cache != nullptr) {
        self->glyph_cache = sw_glyph_cache_new(self->perm_arena, self->font_cache, self->render_mode);
    }
    return self->glyph_cache;
}

static void render(void* user_data, FlSurface* surface_handle, const FlRenderFrame* frame) {
    perf_scope("sr_render");
    RenderData* self = user_data;
    SwSurface* surface = (SwSurface*)surface_handle;

    // Advance the SW pixel pool's frame clock: drives eviction LRU timestamps and the periodic
    // evicted-metadata cleanup.
    image_cache_update(self->image_cache, self->image_cache_frame++);

    self->background = background_from_frame(&frame->background);

    UiRenderCommandsArray render_commands = sr_ir_to_render_commands(&(SrIrRequest) {
        .commands_arena = self->frame_arena,
        .state_arena = self->frame_arena_2,
        .glyph_cache = ensure_glyph_cache(self),
        .ir_commands = frame->commands,
        .ir_bytes = frame->bytes,
        .global_alpha = frame->global_alpha,
    });

    self->render_commands = render_commands.commands;

#ifdef FLOWI_UI_INSPECTOR
    // Each scaled-texture command this frame drove one calculate_shuffle_tables rebuild in sr_command_setup.
    self->stat_scaled_image_setups = 0;
    for (int i = 0; i < render_commands.count; i++) {
        if (render_commands.commands[i].mask & RenderMask_TextureScaled) {
            self->stat_scaled_image_setups++;
        }
    }
#endif

    bin_tiles(self, surface, self->frame_arena, render_commands);
    render_tiles(self, surface);

    fl_arena_rewind(self->frame_arena);
    fl_arena_rewind(self->frame_arena_2);
}

// Optional pixel-readback capability: hand back the offscreen RGB24 buffer the last frame rendered into. The SW
// buffer is unpadded, so stride is exactly screen_width * 3.
static bool read_pixels(void* user_data, FlSurface* surface_handle, FlPixelReadback* out) {
    (void)user_data;
    SwSurface* surface = (SwSurface*)surface_handle;
    *out = (FlPixelReadback) {
        .pixels = surface->rgb24_output_buffer,
        .width = (u32)surface->screen_width,
        .height = (u32)surface->screen_height,
        .stride = (u32)(surface->screen_width * 3),
    };
    return true;
}

#ifdef FLOWI_UI_INSPECTOR
// Hand back the SW work counters the last frame accumulated in bin_tiles. render_commands is left untouched.
void sw_renderer_read_frame_stats(void* user_data, FlFrameStats* out) {
    RenderData* self = user_data;
    out->bin_alloc_bytes = self->stat_bin_alloc_bytes;
    out->clip_broadcasts = self->stat_clip_broadcasts;
    out->scaled_image_setups = self->stat_scaled_image_setups;
}
#endif

// Tear down every arena the backend allocated plus any lazily-built glyph cache. RenderData itself lives in
// the host arena passed to create, so it is not freed here.
static void destroy(void* user_data) {
    RenderData* self = user_data;
    // The SW pixel pool is a process singleton shared across backends; this drops the backend's reference.
    if (self->image_cache != nullptr) {
        image_cache_destroy(self->image_cache);
        self->image_cache = nullptr;
    }
    if (self->glyph_cache != nullptr) {
        sw_glyph_cache_destroy(self->glyph_cache);
        self->glyph_cache = nullptr;
    }
    fl_arena_destroy(self->tile_arena);
    fl_arena_destroy(self->frame_arena_2);
    fl_arena_destroy(self->frame_arena);
    // Per-surface arenas belong to their SwSurface; perm_arena belongs to the host.
}

// Rebind the shared font layer and drop any glyph cache bound to the previous one; the next render rebuilds it.
static void set_font_cache(void* user_data, struct FlFontCache* font_cache) {
    RenderData* self = user_data;
    self->font_cache = font_cache;
    if (self->glyph_cache != nullptr) {
        sw_glyph_cache_destroy(self->glyph_cache);
        self->glyph_cache = nullptr;
    }
}

FlRenderBackend create_software_renderer(struct FlArena* arena, struct FlFontCache* font_cache,
                                         FontRenderMode render_mode) {
    RenderData* render_data = arena_alloc_zero(arena, RenderData);
    render_data->perm_arena = arena;
    render_data->frame_arena = arena_new();
    render_data->frame_arena_2 = arena_new();
    render_data->tile_arena = arena_new();
    render_data->font_cache = font_cache;
    render_data->render_mode = render_mode;

    // The SW realized-pixel pool is a process singleton created on first use.
    render_data->image_cache = image_cache_new(arena, nullptr, (ImageCacheConfig) { 0 });

    // Per-tile scratch is fixed-size and resolution-independent.
    render_data->tile_buffer = arena_alloc_array(render_data->tile_arena, i16, TILE_STRIDE * TILE_WIDTH * 6);

    // No surface is created here: the host builds per-window output buffers via the surface seam.

    return (FlRenderBackend) {
        .user_data = render_data,
        .create_surface = sw_create_surface,
        .destroy_surface = sw_destroy_surface,
        .change_resolution = change_resolution,
        .render = render,
        .destroy = destroy,
        .scale_changed = scale_changed,
        .set_font_cache = set_font_cache,
        .prepare_image = sw_prepare_image,
        .read_pixels = read_pixels,
        // Runs prepare_image in job context; thread-confined backends (GL, Metal) leave this unset.
        .prepare_image_on_job = true,
    };
}
