// Software backend: neutral render IR -> internal RenderCommand list.
//
// The neutral IR (ui/render_ir.h) carries drawing intent only: colors are not premultiplied, the frame's
// global_alpha is not folded in, gradients are four per-corner colors, rounded rects are not pre-split, and
// resources are referenced by handle, never by baked pixels. This translator turns all of that into the
// software renderer's internal RenderCommand form.

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
#include <ui/image/image.h>
#include <ui/image/image_cache.h>
#include <ui/render_ir.h>
#include <assert.h>
#include <math.h>

// Color helpers: the IR stores raw intent; folding global_alpha and premultiplying happens here.

static inline u8 sr_fold_global_alpha(u8 a, u8 global_alpha) {
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

static bool image_has_src_rect(const FlCmdImage* im) {
    return im->src.w > 0.0f && im->src.h > 0.0f;
}

static bool image_src_rect(const FlCmdImage* im, const ImageData* src, FlRect* out) {
    *out = (FlRect) { 0.0f, 0.0f, (float)src->width, (float)src->height };
    if (!image_has_src_rect(im)) {
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

static inline float sr_clamp_radius(float radius, float max_radius) {
    return fminf(fmaxf(radius, 0.0f), max_radius);
}

static inline RenderCommand* sr_alloc_command(const SrIrRequest* req) {
    return arena_alloc_zero(req->commands_arena, RenderCommand);
}

// FillRect: solid (four equal corners) or gradient (four differing). Gradient setup is deferred to
// sr_command_setup; rounded corners are clamped and split here.

static bool fill_corners_are_solid(const FlColor corner[4]) {
    return sr_color_eq(corner[0], corner[1]) && sr_color_eq(corner[0], corner[2]) && sr_color_eq(corner[0], corner[3]);
}

static void set_solid_fill(const SrIrRequest* req, RenderCommand* cmd, FlColor color) {
    cmd->mask = RenderMask_DrawQuad;
    if (sr_fold_global_alpha(color.a, req->global_alpha) < 255) {
        cmd->mask |= RenderMask_BlendWithBackground;
    }
    cmd->corner_colors = sr_color_straight(color, req->global_alpha);
}

// Four corners packed as straight RGBA in [TL,TR,BL,BR] order.
static void set_gradient_fill(const SrIrRequest* req, RenderCommand* cmd, const FlColor corner[4]) {
    u8 packed[16];
    bool translucent = false;
    for (int i = 0; i < 4; ++i) {
        u8 alpha = sr_fold_global_alpha(corner[i].a, req->global_alpha);
        packed[i * 4 + 0] = corner[i].r;
        packed[i * 4 + 1] = corner[i].g;
        packed[i * 4 + 2] = corner[i].b;
        packed[i * 4 + 3] = alpha;
        translucent |= alpha < 255;
    }
    cmd->corner_colors = u8x16_load_unaligned(packed, 0);
    cmd->mask = RenderMask_DrawQuad | RenderMask_ColorGradient;
    if (translucent) {
        cmd->mask |= RenderMask_BlendWithBackground;
    }
}

static void emit_fill_rect(const SrIrRequest* req, const FlCmdFillRect* fr) {
    RenderCommand cmd = { 0 };
    cmd.aabb = sr_rect_to_aabb(fr->rect);

    if (fill_corners_are_solid(fr->corner)) {
        set_solid_fill(req, &cmd, fr->corner[0]);
    } else {
        set_gradient_fill(req, &cmd, fr->corner);
    }

    // Clamp corner radii to half the smaller dimension.
    float max_radius = fminf(fr->rect.w, fr->rect.h) * 0.5f;
    f32x4 corners = f32x4_new(sr_clamp_radius(fr->radius.tl, max_radius), sr_clamp_radius(fr->radius.tr, max_radius),
                              sr_clamp_radius(fr->radius.bl, max_radius), sr_clamp_radius(fr->radius.br, max_radius));
    sr_command_split_rounded(req->commands_arena, &cmd, corners);
}

// Border: per-side widths plus an outer corner radius, emitted as corner and edge quads.

// The color and mask shared by every quad of one border.
typedef struct BorderStyle {
    u8x16 color;
    RenderMask base_mask;
} BorderStyle;

typedef struct BorderCorner {
    float x, y;
    float outer_radius, inner_radius;
    RenderCorner corner;
} BorderCorner;

typedef struct BorderEdge {
    float x0, y0, x1, y1;
} BorderEdge;

static void add_border_corner(const SrIrRequest* req, const BorderStyle* style, const BorderCorner* c) {
    if (c->outer_radius <= 0) {
        return;
    }
    RenderCommand* cmd = sr_alloc_command(req);
    cmd->aabb = f32x4_new(c->x, c->y, c->x + c->outer_radius, c->y + c->outer_radius);
    cmd->corner_colors = style->color;
    cmd->mask = style->base_mask | RenderMask_Border | RenderMask_Corners | render_mask_corner(c->corner);
    cmd->radius = c->outer_radius;
    cmd->inner_radius = c->inner_radius;
    cmd->global_alpha = req->global_alpha;
}

static void add_border_edge(const SrIrRequest* req, const BorderStyle* style, const BorderEdge* e) {
    if (e->x1 <= e->x0 || e->y1 <= e->y0) {
        return;
    }
    RenderCommand* cmd = sr_alloc_command(req);
    cmd->aabb = f32x4_new(e->x0, e->y0, e->x1, e->y1);
    cmd->corner_colors = style->color;
    cmd->mask = style->base_mask;
    cmd->global_alpha = req->global_alpha;
}

static bool border_has_no_width(const FlCmdBorder* b) {
    return b->w_l == 0 && b->w_r == 0 && b->w_t == 0 && b->w_b == 0;
}

static float border_inner_radius(float radius, float width_a, float width_b) {
    return fmaxf(0.0f, radius - fmaxf(width_a, width_b));
}

// Rounded corners: outer_radius = corner_radius, inner_radius = corner_radius - border_width.
static void add_border_corners(const SrIrRequest* req, const BorderStyle* style, const FlCmdBorder* b,
                               FlCornerRadius r) {
    float right = b->rect.x + b->rect.w;
    float bottom = b->rect.y + b->rect.h;

    add_border_corner(req, style,
                      &(BorderCorner) {
                          .x = b->rect.x,
                          .y = b->rect.y,
                          .outer_radius = r.tl,
                          .inner_radius = border_inner_radius(r.tl, b->w_l, b->w_t),
                          .corner = RenderCorner_TopLeft,
                      });
    add_border_corner(req, style,
                      &(BorderCorner) {
                          .x = right - r.tr,
                          .y = b->rect.y,
                          .outer_radius = r.tr,
                          .inner_radius = border_inner_radius(r.tr, b->w_r, b->w_t),
                          .corner = RenderCorner_TopRight,
                      });
    add_border_corner(req, style,
                      &(BorderCorner) {
                          .x = b->rect.x,
                          .y = bottom - r.bl,
                          .outer_radius = r.bl,
                          .inner_radius = border_inner_radius(r.bl, b->w_l, b->w_b),
                          .corner = RenderCorner_BottomLeft,
                      });
    add_border_corner(req, style,
                      &(BorderCorner) {
                          .x = right - r.br,
                          .y = bottom - r.br,
                          .outer_radius = r.br,
                          .inner_radius = border_inner_radius(r.br, b->w_r, b->w_b),
                          .corner = RenderCorner_BottomRight,
                      });
}

// b->stroke is ignored: every border is drawn solid.
static void emit_border(const SrIrRequest* req, const FlCmdBorder* b) {
    if (border_has_no_width(b)) {
        return;
    }

    BorderStyle style = { .color = sr_color_straight(b->color, req->global_alpha), .base_mask = RenderMask_DrawQuad };
    if (sr_fold_global_alpha(b->color.a, req->global_alpha) < 255) {
        style.base_mask |= RenderMask_BlendWithBackground;
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
    add_border_corners(req, &style, b, (FlCornerRadius) { r_tl, r_tr, r_bl, r_br });

    add_border_edge(req, &style, &(BorderEdge) { .x0 = x + r_tl, .y0 = y, .x1 = x + width - r_tr, .y1 = y + b->w_t });
    add_border_edge(
        req, &style,
        &(BorderEdge) { .x0 = x + r_bl, .y0 = y + height - b->w_b, .x1 = x + width - r_br, .y1 = y + height });
    add_border_edge(req, &style, &(BorderEdge) { .x0 = x, .y0 = y + r_tl, .x1 = x + b->w_l, .y1 = y + height - r_bl });
    add_border_edge(
        req, &style,
        &(BorderEdge) { .x0 = x + width - b->w_r, .y0 = y + r_tr, .x1 = x + width, .y1 = y + height - r_br });
}

// TextRun: re-resolve each pre-positioned glyph by id through the glyph cache and emit it.

static void emit_text_run(const SrIrRequest* req, const FlCmdTextRun* tr) {
    if (req->glyph_cache == nullptr) {
        return;
    }
    const FlGlyph* glyphs = (const FlGlyph*)(tr + 1);
    u8x16 color = sr_color_premul(tr->color, req->global_alpha);
    // Glyphs are blitted 1:1, so only run->scale == 1.0 is honored. The run's size is a quarter-px key, rounded
    // to whole pixels for rasterization.
    u16 size_px = font_cache_q4_to_whole_px(tr->size);
    for (u32 i = 0; i < tr->glyph_count; ++i) {
        const Glyph* glyph = sw_glyph_cache_get(req->glyph_cache, tr->font, size_px, glyphs[i].glyph_id);
        sr_emit_glyph(req->commands_arena, glyph, glyphs[i].x, glyphs[i].y, color, req->global_alpha);
    }
}

// Image: color mode (static handle or dynamic RGBA buffer) and alpha-coverage mode (bitmap glyphs / alpha-only
// dynamic images). Source resolution and format conversion happen here, never in the IR.

// A single-channel mask colorized by the tint, the grayscale RenderText path. The source is always a dynamic
// neutral buffer.
static void emit_alpha_coverage_image(const SrIrRequest* req, const FlCmdImage* im) {
    RenderCommand* cmd = sr_alloc_command(req);
    cmd->mask = RenderMask_DrawQuad | RenderMask_Text;
    cmd->global_alpha = req->global_alpha;
    cmd->render_text.aabb = sr_rect_to_aabb(im->dst);
    cmd->render_text.color = sr_color_premul(im->tint, req->global_alpha);
    cmd->render_text.data_r = (const u8*)im->source.dynamic.pixels;
    cmd->render_text.data_g = nullptr; // nullptr signals grayscale mode
    cmd->render_text.data_b = nullptr;
    // stride 0 means tightly packed. Coverage is 1 byte/pixel.
    u32 cov_stride = im->source.dynamic.stride;
    if (cov_stride == 0) {
        cov_stride = im->source.dynamic.w;
    }
    cmd->render_text.stride = (int)cov_stride;
}

// Resolve a static handle to a planar ImageData the texture sampler can read. Null when the image is not ready.
static ImageData* resolve_static_image(const SrIrRequest* req, const FlCmdImage* im) {
    ImageData* src = image_cache_get_data((ImageHandle)im->source.handle);
    if (src == nullptr || !sr_image_needs_conversion(src)) {
        return src;
    }
    // RGBA keeps its alpha plane for blending; XRGB drops the unused X byte.
    bool keep_alpha = (src->format == FlImageFormat_RGBA);
    ImageData* converted = sr_convert_to_planar(src, keep_alpha, req->state_arena);
    return converted != nullptr ? converted : src;
}

// Dynamic neutral RGBA buffer: deinterleave to 3-plane RGB.
static ImageData* resolve_dynamic_image(const SrIrRequest* req, const FlCmdImage* im) {
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
    return sr_convert_to_planar(&wrap, false, req->state_arena);
}

// Where the image lands: the source rectangle to sample and whether it is drawn at 1:1.
typedef struct ImagePlacement {
    bool has_src_rect;
    FlRect src_rect;
    bool same_size;
} ImagePlacement;

// False when the image has to be dropped: an empty source rectangle, or a sub-image drawn at another size.
static bool image_placement(const FlCmdImage* im, const ImageData* src, ImagePlacement* out) {
    out->has_src_rect = image_src_rect(im, src, &out->src_rect);
    if (image_has_src_rect(im) && !out->has_src_rect) {
        return false;
    }

    const float epsilon = 0.5f;
    bool width_matches = fabsf(im->dst.w - out->src_rect.w) < epsilon;
    bool height_matches = fabsf(im->dst.h - out->src_rect.h) < epsilon;
    out->same_size = width_matches && height_matches;
    if (out->has_src_rect && !out->same_size) {
        FL_ASSERT_DEBUG(false && "SW image_sub currently supports only dst size equal to source rect size");
        return false;
    }
    return true;
}

static void emit_image(const SrIrRequest* req, const FlCmdImage* im) {
    if (im->mode == FlImageMode_AlphaCoverage) {
        emit_alpha_coverage_image(req, im);
        return;
    }

    // A destination under one whole pixel wide is dropped before calculate_shuffle_tables sees it.
    if ((int)im->dst.w <= 0) {
        return;
    }

    ImageData* src
        = im->source.kind == FlImageKind_Static ? resolve_static_image(req, im) : resolve_dynamic_image(req, im);
    if (src == nullptr) {
        return;
    }

    ImagePlacement place;
    if (!image_placement(im, src, &place)) {
        return;
    }

    RenderCommand* cmd = sr_alloc_command(req);
    cmd->mask = RenderMask_DrawQuad | RenderMask_BlendWithBackground; // images always blend (may carry alpha)
    cmd->global_alpha = req->global_alpha;
    cmd->image = src;
    cmd->aabb = sr_rect_to_aabb(im->dst);
    cmd->corner_colors = sr_color_raw(im->tint); // raw tint; white = no tint
    if (place.has_src_rect) {
        cmd->image_src_x = (int)place.src_rect.x;
        cmd->image_src_y = (int)place.src_rect.y;
    }

    cmd->mask |= place.same_size ? RenderMask_TextureSameSize : RenderMask_TextureScaled;
}

static void emit_clip_push(const SrIrRequest* req, const FlCmdClipPush* clip) {
    RenderCommand* cmd = sr_alloc_command(req);
    cmd->mask = RenderMask_ClipRectBegin;
    cmd->aabb = sr_rect_to_aabb(clip->rect);
}

static void emit_clip_pop(const SrIrRequest* req) {
    RenderCommand* cmd = sr_alloc_command(req);
    cmd->mask = RenderMask_ClipRectEnd;
}

static void emit_ir_command(const SrIrRequest* req, const FlCmdHeader* header) {
    switch ((FlCmdKind)header->kind) {
        case FlCmd_FillRect:
            emit_fill_rect(req, (const FlCmdFillRect*)(header + 1));
            break;
        case FlCmd_Border:
            emit_border(req, (const FlCmdBorder*)(header + 1));
            break;
        case FlCmd_TextRun:
            emit_text_run(req, (const FlCmdTextRun*)(header + 1));
            break;
        case FlCmd_Image:
            emit_image(req, (const FlCmdImage*)(header + 1));
            break;
        case FlCmd_ClipPush:
            emit_clip_push(req, (const FlCmdClipPush*)(header + 1));
            break;
        case FlCmd_ClipPop:
            emit_clip_pop(req);
            break;
        default:
            break; // optional-tier kinds: a SW backend may ignore them
    }
}

UiRenderCommandsArray sr_ir_to_render_commands(const SrIrRequest* req) {
    perf_scope_function();

    FlArena* commands_arena = req->commands_arena;

    // Align first: arena_pos_type returns an aligned position that may be past the current one, which would
    // trip arena_count's bounds assertion.
    fl_arena_align(commands_arena, _Alignof(RenderCommand));
    RenderCommand* start = arena_pos_type(commands_arena, RenderCommand);

    const u8* p = (const u8*)req->ir_commands;
    const u8* end = p + req->ir_bytes;
    while (p < end) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        emit_ir_command(req, header);
        p += header->byte_size;
    }

    int count = arena_count(commands_arena, start, RenderCommand);

    for_count(i, count) {
        sr_command_setup(req->state_arena, &start[i]);
    }

    return (UiRenderCommandsArray) { .commands = start, .count = count };
}
