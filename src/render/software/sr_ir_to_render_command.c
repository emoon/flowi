///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Software backend: neutral render IR -> internal RenderCommand list.
//
// The neutral IR (ui/render_ir.h) carries drawing intent only: colors are not premultiplied, the frame's
// global_alpha is not folded in, gradients are four per-corner colors, rounded rects are not pre-split, and
// resources are referenced by handle, never by baked pixels. This translator turns all of that into the
// software renderer's internal RenderCommand form.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sr_ir_to_render_command.h"

#include <core/core.h>
#include <core/arena.h>
#include <core/math.h>
#include <core/perf_scope.h>
#include <core/simd.h>
#include "sr_command.h"
#include "sr_image_convert.h"
#include "sr_render_helpers.h"
#include "sw_glyph_cache.h"
#include <ui/font_cache.h>
#include <ui/image.h>
#include <ui/image_cache.h>
#include <ui/render_ir.h>
#include <assert.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Color helpers - the IR stores raw intent; folding global_alpha and premultiplying happens here.

static inline u8 sr_fold_global_alpha(u8 a, u8 global_alpha) {
    // Rounded: (a*ga+127)/255.
    return (global_alpha == 255) ? a : (u8)((a * global_alpha + 127) / 255);
}

// Premultiplied solid color replicated across four pixels (fills, borders, text tint).
static inline u8x16 sr_color_premul(FlColor c, u8 global_alpha) {
    u8 a = sr_fold_global_alpha(c.a, global_alpha);
    const u8 colors[16] = { c.r, c.g, c.b, a, c.r, c.g, c.b, a, c.r, c.g, c.b, a, c.r, c.g, c.b, a };
    return sr_premultiply_rgba(u8x16_load_unaligned(colors, 0));
}

// Raw (non-premultiplied) tint replicated across four pixels. The frame's global_alpha rides in
// RenderCommand.global_alpha, not in the tint.
static inline u8x16 sr_color_raw(FlColor c) {
    const u8 colors[16] = { c.r, c.g, c.b, c.a, c.r, c.g, c.b, c.a, c.r, c.g, c.b, c.a, c.r, c.g, c.b, c.a };
    return u8x16_load_unaligned(colors, 0);
}

// Straight (non-premultiplied) solid color with global_alpha folded into the alpha channel, replicated across
// four pixels. Fills and borders composite straight-over (out = rgb*a + bg*(1-a)).
static inline u8x16 sr_color_straight(FlColor c, u8 global_alpha) {
    u8 a = sr_fold_global_alpha(c.a, global_alpha);
    const u8 colors[16] = { c.r, c.g, c.b, a, c.r, c.g, c.b, a, c.r, c.g, c.b, a, c.r, c.g, c.b, a };
    return u8x16_load_unaligned(colors, 0);
}

static inline bool sr_color_eq(FlColor a, FlColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static inline f32x4 sr_rect_to_aabb(FlRect r) {
    return f32x4_new(r.x, r.y, r.x + r.w, r.y + r.h);
}

static bool image_src_rect(const FlCmdImage* im, const ImageData* src, FlRect* out) {
    *out = (FlRect) { 0.0f, 0.0f, (float)src->width, (float)src->height };
    if (im->src.w <= 0.0f || im->src.h <= 0.0f) {
        return false;
    }

    float x0 = clamp(im->src.x, 0.0f, (float)src->width);
    float y0 = clamp(im->src.y, 0.0f, (float)src->height);
    float x1 = clamp(im->src.x + im->src.w, x0, (float)src->width);
    float y1 = clamp(im->src.y + im->src.h, y0, (float)src->height);
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return out->w > 0.0f && out->h > 0.0f;
}

// Clamp a corner radius to [0, max].
static inline float sr_clamp_radius(float radius, float max_radius) {
    return fminf(fmaxf(radius, 0.0f), max_radius);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FillRect - solid (four equal corners) or gradient (four differing). Gradient setup is deferred to
// sr_command_setup; rounded corners are clamped and split here.

static void emit_fill_rect(FlArena* commands_arena, const FlCmdFillRect* fr, u8 global_alpha) {
    RenderCommand cmd = { 0 };
    cmd.aabb = sr_rect_to_aabb(fr->rect);

    bool solid = sr_color_eq(fr->corner[0], fr->corner[1]) && sr_color_eq(fr->corner[0], fr->corner[2])
                 && sr_color_eq(fr->corner[0], fr->corner[3]);

    if (solid) {
        cmd.mask = RenderMask_DrawQuad;
        if (sr_fold_global_alpha(fr->corner[0].a, global_alpha) < 255) {
            cmd.mask |= RenderMask_BlendWithBackground;
        }
        cmd.corner_colors = sr_color_straight(fr->corner[0], global_alpha);
    } else {
        // Four corners packed as straight RGBA in [TL,TR,BL,BR] order.
        u8 packed[16];
        for (int i = 0; i < 4; ++i) {
            packed[i * 4 + 0] = fr->corner[i].r;
            packed[i * 4 + 1] = fr->corner[i].g;
            packed[i * 4 + 2] = fr->corner[i].b;
            packed[i * 4 + 3] = sr_fold_global_alpha(fr->corner[i].a, global_alpha);
        }
        cmd.corner_colors = u8x16_load_unaligned(packed, 0);
        cmd.mask = RenderMask_DrawQuad | RenderMask_ColorGradient;
        if (packed[3] < 255 || packed[7] < 255 || packed[11] < 255 || packed[15] < 255) {
            cmd.mask |= RenderMask_BlendWithBackground;
        }
    }

    // Clamp corner radii to half the smaller dimension.
    float max_radius = fminf(fr->rect.w, fr->rect.h) * 0.5f;
    f32x4 corners = f32x4_new(sr_clamp_radius(fr->radius.tl, max_radius), sr_clamp_radius(fr->radius.tr, max_radius),
                              sr_clamp_radius(fr->radius.bl, max_radius), sr_clamp_radius(fr->radius.br, max_radius));
    sr_command_split_rounded(commands_arena, &cmd, corners);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Border - per-side widths plus an outer corner radius, emitted as corner and edge quads.

static void add_border_corner(FlArena* commands_arena, u8x16 color, RenderMask base_mask, float x, float y,
                              float outer_radius, float inner_radius, RenderCorner corner, u8 global_alpha) {
    RenderCommand* cmd = arena_alloc_zero(commands_arena, RenderCommand);
    cmd->aabb = f32x4_new(x, y, x + outer_radius, y + outer_radius);
    cmd->corner_colors = color;
    cmd->mask = base_mask | RenderMask_Border | RenderMask_Corners | render_mask_corner(corner);
    cmd->radius = outer_radius;
    cmd->inner_radius = inner_radius;
    cmd->global_alpha = global_alpha;
}

static void add_border_edge(FlArena* commands_arena, u8x16 color, RenderMask base_mask, float x0, float y0, float x1,
                            float y1, u8 global_alpha) {
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    RenderCommand* cmd = arena_alloc_zero(commands_arena, RenderCommand);
    cmd->aabb = f32x4_new(x0, y0, x1, y1);
    cmd->corner_colors = color;
    cmd->mask = base_mask;
    cmd->global_alpha = global_alpha;
}

// b->stroke is ignored: every border is drawn solid.
static void emit_border(FlArena* commands_arena, const FlCmdBorder* b, u8 global_alpha) {
    float w_left = b->w_l;
    float w_right = b->w_r;
    float w_top = b->w_t;
    float w_bottom = b->w_b;

    if (w_left == 0 && w_right == 0 && w_top == 0 && w_bottom == 0) {
        return;
    }

    u8x16 color = sr_color_straight(b->color, global_alpha);
    RenderMask base_mask = RenderMask_DrawQuad;
    if (sr_fold_global_alpha(b->color.a, global_alpha) < 255) {
        base_mask |= RenderMask_BlendWithBackground;
    }

    float x = b->rect.x;
    float y = b->rect.y;
    float width = b->rect.w;
    float height = b->rect.h;

    // Clamp corner radii to half of element dimensions.
    float max_radius = fminf(width, height) * 0.5f;
    float r_tl = sr_clamp_radius(b->radius.tl, max_radius);
    float r_tr = sr_clamp_radius(b->radius.tr, max_radius);
    float r_bl = sr_clamp_radius(b->radius.bl, max_radius);
    float r_br = sr_clamp_radius(b->radius.br, max_radius);

    // Rounded corners: outer_radius = corner_radius, inner_radius = corner_radius - border_width.
    if (r_tl > 0) {
        float inner = fmaxf(0.0f, r_tl - fmaxf(w_left, w_top));
        add_border_corner(commands_arena, color, base_mask, x, y, r_tl, inner, RenderCorner_TopLeft, global_alpha);
    }
    if (r_tr > 0) {
        float inner = fmaxf(0.0f, r_tr - fmaxf(w_right, w_top));
        add_border_corner(commands_arena, color, base_mask, x + width - r_tr, y, r_tr, inner, RenderCorner_TopRight,
                          global_alpha);
    }
    if (r_bl > 0) {
        float inner = fmaxf(0.0f, r_bl - fmaxf(w_left, w_bottom));
        add_border_corner(commands_arena, color, base_mask, x, y + height - r_bl, r_bl, inner, RenderCorner_BottomLeft,
                          global_alpha);
    }
    if (r_br > 0) {
        float inner = fmaxf(0.0f, r_br - fmaxf(w_right, w_bottom));
        add_border_corner(commands_arena, color, base_mask, x + width - r_br, y + height - r_br, r_br, inner,
                          RenderCorner_BottomRight, global_alpha);
    }

    add_border_edge(commands_arena, color, base_mask, x + r_tl, y, x + width - r_tr, y + w_top, global_alpha);
    add_border_edge(commands_arena, color, base_mask, x + r_bl, y + height - w_bottom, x + width - r_br, y + height,
                    global_alpha);
    add_border_edge(commands_arena, color, base_mask, x, y + r_tl, x + w_left, y + height - r_bl, global_alpha);
    add_border_edge(commands_arena, color, base_mask, x + width - w_right, y + r_tr, x + width, y + height - r_br,
                    global_alpha);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TextRun - re-resolve each pre-positioned glyph by id through the glyph cache and emit it.

static void emit_text_run(FlArena* commands_arena, SwGlyphCache* glyph_cache, const FlCmdTextRun* tr, u8 global_alpha) {
    if (glyph_cache == nullptr) {
        return;
    }
    const FlGlyph* glyphs = (const FlGlyph*)(tr + 1);
    u8x16 color = sr_color_premul(tr->color, global_alpha);
    // Glyphs are blitted 1:1, so only run->scale == 1.0 is honored. The run's size is a quarter-px key, rounded
    // to whole pixels for rasterization.
    u16 size_px = font_cache_q4_to_whole_px(tr->size);
    for (u32 i = 0; i < tr->glyph_count; ++i) {
        const Glyph* glyph = sw_glyph_cache_get(glyph_cache, tr->font, size_px, glyphs[i].glyph_id);
        sr_emit_glyph(commands_arena, glyph, glyphs[i].x, glyphs[i].y, color, global_alpha);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image - color mode (static handle or dynamic RGBA buffer) and alpha-coverage mode (bitmap glyphs / alpha-only
// dynamic images). Source resolution and format conversion happen here, never in the IR.

static void emit_image(FlArena* commands_arena, FlArena* state_arena, const FlCmdImage* im, u8 global_alpha) {
    // Alpha-coverage: a single-channel mask colorized by the tint - the grayscale RenderText path. The source
    // is always a dynamic neutral buffer.
    if (im->mode == FlImageMode_AlphaCoverage) {
        RenderCommand* cmd = arena_alloc_zero(commands_arena, RenderCommand);
        cmd->mask = RenderMask_DrawQuad | RenderMask_Text;
        cmd->global_alpha = global_alpha;
        cmd->render_text.aabb = sr_rect_to_aabb(im->dst);
        cmd->render_text.color = sr_color_premul(im->tint, global_alpha);
        cmd->render_text.data_r = (const u8*)im->source.dynamic.pixels;
        cmd->render_text.data_g = nullptr; // nullptr signals grayscale mode
        cmd->render_text.data_b = nullptr;
        // stride 0 means tightly packed. Coverage is 1 byte/pixel.
        u32 cov_stride = im->source.dynamic.stride;
        if (cov_stride == 0) {
            cov_stride = im->source.dynamic.w;
        }
        cmd->render_text.stride = (int)cov_stride;
        return;
    }

    // A destination under one whole pixel wide is dropped before calculate_shuffle_tables sees it.
    if ((int)im->dst.w <= 0) {
        return;
    }

    // Color mode: resolve to a planar ImageData the texture sampler can read.
    ImageData* src = nullptr;
    if (im->source.kind == FlImageKind_Static) {
        src = image_cache_get_data((ImageHandle)im->source.handle);
        if (src == nullptr) {
            return; // image not ready/invalid
        }
        if (sr_image_needs_conversion(src)) {
            // RGBA keeps its alpha plane for blending; XRGB drops the unused X byte.
            bool keep_alpha = (src->format == FlImageFormat_RGBA);
            ImageData* converted = sr_convert_to_planar(src, keep_alpha, state_arena);
            if (converted != nullptr) {
                src = converted;
            }
        }
    } else {
        // Dynamic neutral RGBA buffer: deinterleave to 3-plane RGB.
        ImageData wrap = { 0 };
        wrap.data = (u8*)im->source.dynamic.pixels;
        wrap.width = (int)im->source.dynamic.w;
        wrap.height = (int)im->source.dynamic.h;
        wrap.padded_width = wrap.width;
        wrap.padded_height = wrap.height;
        // stride 0 means tightly packed. RGBA is 4 bytes/pixel.
        u32 dyn_stride = im->source.dynamic.stride;
        if (dyn_stride == 0) {
            dyn_stride = im->source.dynamic.w * 4;
        }
        wrap.stride = (int)dyn_stride;
        wrap.channels = 4;
        wrap.format = FlImageFormat_RGBA;
        src = sr_convert_to_planar(&wrap, false, state_arena);
        if (src == nullptr) {
            return;
        }
    }

    FlRect src_rect;
    bool has_src_rect = image_src_rect(im, src, &src_rect);
    if (im->src.w > 0.0f && im->src.h > 0.0f && !has_src_rect) {
        return;
    }

    const float epsilon = 0.5f;
    bool width_matches = fabsf(im->dst.w - src_rect.w) < epsilon;
    bool height_matches = fabsf(im->dst.h - src_rect.h) < epsilon;
    if (has_src_rect && (!width_matches || !height_matches)) {
        FL_ASSERT_DEBUG(width_matches && height_matches
                        && "SW image_sub currently supports only dst size equal to source rect size");
        return;
    }

    RenderCommand* cmd = arena_alloc_zero(commands_arena, RenderCommand);
    cmd->mask = RenderMask_DrawQuad | RenderMask_BlendWithBackground; // images always blend (may carry alpha)
    cmd->global_alpha = global_alpha;
    cmd->image = src;
    cmd->aabb = sr_rect_to_aabb(im->dst);
    cmd->corner_colors = sr_color_raw(im->tint); // raw tint; white = no tint
    if (has_src_rect) {
        cmd->image_src_x = (int)src_rect.x;
        cmd->image_src_y = (int)src_rect.y;
    }

    cmd->mask |= (width_matches && height_matches) ? RenderMask_TextureSameSize : RenderMask_TextureScaled;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UiRenderCommandsArray sr_ir_to_render_commands(FlArena* commands_arena, FlArena* state_arena, SwGlyphCache* glyph_cache,
                                               const void* ir_commands, size_t ir_bytes, u8 global_alpha) {
    perf_scope_function();

    // Align first: arena_pos_type returns an aligned position that may be past the current one, which would
    // trip arena_count's bounds assertion.
    fl_arena_align(commands_arena, _Alignof(RenderCommand));
    RenderCommand* start = arena_pos_type(commands_arena, RenderCommand);

    const u8* p = (const u8*)ir_commands;
    const u8* end = p + ir_bytes;
    while (p < end) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        switch ((FlCmdKind)header->kind) {
            case FlCmd_FillRect:
                emit_fill_rect(commands_arena, (const FlCmdFillRect*)(header + 1), global_alpha);
                break;
            case FlCmd_Border:
                emit_border(commands_arena, (const FlCmdBorder*)(header + 1), global_alpha);
                break;
            case FlCmd_TextRun:
                emit_text_run(commands_arena, glyph_cache, (const FlCmdTextRun*)(header + 1), global_alpha);
                break;
            case FlCmd_Image:
                emit_image(commands_arena, state_arena, (const FlCmdImage*)(header + 1), global_alpha);
                break;
            case FlCmd_ClipPush: {
                const FlCmdClipPush* clip = (const FlCmdClipPush*)(header + 1);
                RenderCommand* cmd = arena_alloc_zero(commands_arena, RenderCommand);
                cmd->mask = RenderMask_ClipRectBegin;
                cmd->aabb = sr_rect_to_aabb(clip->rect);
                break;
            }
            case FlCmd_ClipPop: {
                RenderCommand* cmd = arena_alloc_zero(commands_arena, RenderCommand);
                cmd->mask = RenderMask_ClipRectEnd;
                break;
            }
            default:
                break; // optional-tier kinds: a SW backend may ignore them
        }
        p += header->byte_size;
    }

    int count = arena_count(commands_arena, start, RenderCommand);

    for_count(i, count) {
        sr_command_setup(state_arena, &start[i]);
    }

    return (UiRenderCommandsArray) { .commands = start, .count = count };
}
