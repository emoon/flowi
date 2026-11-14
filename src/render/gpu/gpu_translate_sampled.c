#include "gpu_translate.h"
#include "gpu_aabb.h"
#include "gpu_draw_pack.h"
#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_svg_atlas.h"
#include "gpu_svg_translate.h"
#include <core/core.h>
#include <math.h>

// The source sub-rect in image pixels ({0,0,0,0} = the whole image), clamped to the image extent so a
// malformed record can never sample outside it (for atlas cells: a neighboring cell). out is x0, y0, x1, y1.
static void image_src_rect(const FlCmdImage* im, u32 img_w, u32 img_h, f32 out[4]) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = (f32)img_w;
    out[3] = (f32)img_h;
    if (im->src.w > 0.0f && im->src.h > 0.0f) {
        out[0] = fminf(fmaxf(im->src.x, 0.0f), (f32)img_w);
        out[1] = fminf(fmaxf(im->src.y, 0.0f), (f32)img_h);
        out[2] = fminf(fmaxf(im->src.x + im->src.w, out[0]), (f32)img_w);
        out[3] = fminf(fmaxf(im->src.y + im->src.h, out[1]), (f32)img_h);
    }
}

// An image on a sampler slot (large static or dynamic source) and the extent of its texture.
typedef struct SlotRef {
    u32 index;
    u32 w;
    u32 h;
} SlotRef;

// Emit a slot-image command. The caller has already established clip visibility, so a granted slot is never
// wasted on an invisible command.
static void emit_slot_image(GpuTranslate* tr, const FlCmdImage* im, SlotRef image) {
    int s = gpu_image_slots_assign(tr->slots, image.index, im->filter);
    if (s < 0) {
        return; // every slot taken: the command drops, counted in slots->overflow
    }
    f32 src[4];
    image_src_rect(im, image.w, image.h, src);
    gpu_emit(tr, gpu_aabb_from_rect(im->dst),
             (GpuDrawCmd) {
                 .rect = { im->dst.x, im->dst.y, im->dst.w, im->dst.h },
                 .radius = { src[0], src[1], src[2], src[3] },
                 .color = { gpu_pack_tint_premultiplied(im->tint, tr->global_alpha) },
                 .kind = GpuDraw_SlotImage,
                 .aux = (u32)s,
             });
}

static bool emit_atlas_image(GpuTranslate* tr, const FlCmdImage* im) {
    const GpuImageAtlasEntry* entry
        = tr->atlas != nullptr ? gpu_image_atlas_find(tr->atlas, im->source.handle) : nullptr;
    if (entry == nullptr) {
        return false;
    }
    f32 src[4];
    image_src_rect(im, entry->img_w, entry->img_h, src);
    f32 cx = (f32)entry->cell.x;
    f32 cy = (f32)entry->cell.y;
    gpu_emit(tr, gpu_aabb_from_rect(im->dst),
             (GpuDrawCmd) {
                 .rect = { im->dst.x, im->dst.y, im->dst.w, im->dst.h },
                 .radius = { cx + src[0], cy + src[1], cx + src[2], cy + src[3] },
                 .color = { gpu_pack_tint_premultiplied(im->tint, tr->global_alpha) },
                 .kind = GpuDraw_Image,
                 .flags = im->filter == FlImageFilter_Nearest ? GPU_DRAW_IMAGE_NEAREST : 0u,
             });
    return true;
}

// Opaque white ({255,255,255,255}) is the image tint identity ("no modulation"). For a monochrome vector
// icon it is the signal to use the icon's authored fill; any other tint overrides it (recolor, like a glyph).
static bool tint_is_identity(FlColor c) {
    return c.r == 255 && c.g == 255 && c.b == 255 && c.a == 255;
}

// A vector icon resolved to its live SVG-atlas cell, the command kind that samples it and the tint to draw it with.
typedef struct SvgRoute {
    const GpuSvgEntry* entry;
    GpuDrawKind kind;
    FlColor tint;
} SvgRoute;

// Emit a vector-icon command: the premultiplied tint with the frame global_alpha folded. gpu_svg_image_cmd
// forces a multi-color cell's tint to white (its colors are baked), so a record tint only recolors a monochrome
// icon.
static void emit_svg_cell(GpuTranslate* tr, const FlCmdImage* im, SvgRoute route) {
    FlCmdImage routed = gpu_svg_image_cmd(im->source.handle, route.entry->cell, route.entry->cls, route.tint, im->dst);
    gpu_emit(tr, gpu_aabb_from_rect(im->dst),
             (GpuDrawCmd) {
                 .rect = { im->dst.x, im->dst.y, im->dst.w, im->dst.h },
                 // The cell content sub-rect (padding inset) in SVG-atlas texels, x0 y0 x1 y1.
                 .radius = { routed.src.x, routed.src.y, routed.src.x + routed.src.w, routed.src.y + routed.src.h },
                 .color = { gpu_pack_tint_premultiplied(routed.tint, tr->global_alpha) },
                 .kind = route.kind,
             });
}

static bool emit_mono_svg(GpuTranslate* tr, const FlCmdImage* im) {
    const GpuSvgEntry* svg = tr->svg != nullptr ? gpu_svg_atlas_find(tr->svg, im->source.handle) : nullptr;
    if (svg == nullptr) {
        return false;
    }
    // Default tint is the icon's authored fill; a non-identity record tint overrides it.
    emit_svg_cell(tr, im, (SvgRoute) { svg, GpuDraw_Svg, tint_is_identity(im->tint) ? svg->mono_color : im->tint });
    return true;
}

static bool emit_color_svg(GpuTranslate* tr, const FlCmdImage* im) {
    const GpuSvgEntry* svg = tr->svg_color != nullptr ? gpu_svg_atlas_find(tr->svg_color, im->source.handle) : nullptr;
    if (svg == nullptr) {
        return false;
    }
    emit_svg_cell(tr, im, (SvgRoute) { svg, GpuDraw_SvgColor, im->tint });
    return true;
}

static void emit_static_slot_image(GpuTranslate* tr, const FlCmdImage* im) {
    int index = tr->slots != nullptr ? gpu_image_slots_find_static(tr->slots, im->source.handle) : -1;
    if (index < 0 || !gpu_clip_visible(tr, gpu_aabb_from_rect(im->dst))) {
        return; // never prepared (or released), or fully clipped - no slot claimed
    }
    const GpuSlotImage* img = &tr->slots->images[index];
    emit_slot_image(tr, im, (SlotRef) { (u32)index, img->w, img->h });
}

typedef bool (*ImageResolver)(GpuTranslate* tr, const FlCmdImage* im);

// A handle is only ever one kind, so the resolution order is a fall-through, not a precedence.
static const ImageResolver k_resolvers[] = { emit_atlas_image, emit_mono_svg, emit_color_svg };

static void translate_static_image(GpuTranslate* tr, const FlCmdImage* im) {
    for_count(i, sizeof_array(k_resolvers)) {
        if (k_resolvers[i](tr, im)) {
            return;
        }
    }
    emit_static_slot_image(tr, im);
}

static bool dynamic_source_usable(const GpuTranslate* tr, const FlCmdImage* im) {
    const FlImageSource* src = &im->source;
    if (tr->slots == nullptr || src->dynamic.pixels == nullptr) {
        return false;
    }
    return src->dynamic.w != 0 && src->dynamic.h != 0;
}

// Touch marks the upload for this referencing frame - gated behind clip visibility so an invisible command
// uploads nothing.
static void translate_dynamic_image(GpuTranslate* tr, const FlCmdImage* im) {
    if (!dynamic_source_usable(tr, im) || !gpu_clip_visible(tr, gpu_aabb_from_rect(im->dst))) {
        return;
    }
    const FlImageSource* src = &im->source;
    int index = gpu_image_slots_touch_dynamic(tr->slots, src->dynamic.pixels, src->dynamic.w, src->dynamic.h,
                                              src->dynamic.stride, im->mode == FlImageMode_AlphaCoverage);
    if (index < 0) {
        return; // table full - diagnosed by the slots module
    }
    emit_slot_image(tr, im, (SlotRef) { (u32)index, src->dynamic.w, src->dynamic.h });
}

void gpu_translate_image(GpuTranslate* tr, const void* payload) {
    const FlCmdImage* im = payload;
    if (im->dst.w <= 0.0f || im->dst.h <= 0.0f) {
        return;
    }
    if (im->source.kind == FlImageKind_Static) {
        translate_static_image(tr, im);
    } else {
        translate_dynamic_image(tr, im);
    }
}

// One glyph quad at the glyph's baseline pen position offset by the cell's left/top metrics (float pixels
// relative to the pen, y-up FreeType bearing): screen-left = pen.x + left, screen-top = pen.y - top, sized to the
// padded cell. radius carries the source texel rect in the R8 atlas; color[0] the run's tint. A glyph absent from
// the atlas is skipped; an empty glyph (a metrics-only entry, w or h == 0, e.g. space) emits nothing.
//
// The run is rasterized crisp at run->size (a quarter-px key; (run->size / 4) px) and displayed at that size
// times run->scale: the quad's bearing offset and extent scale by run->scale around the pen position. The atlas
// source rect (radius[]) stays in raster texels, so the quad magnifies/minifies the cell's coverage under the
// shader's linear sample.
static void emit_glyph(GpuTranslate* tr, const FlCmdTextRun* run, const FlGlyph* glyph, u32 tint) {
    const GpuGlyphEntry* e = gpu_glyph_atlas_find(tr->glyphs, run->font, run->size, glyph->glyph_id);
    if (e == nullptr) {
        return;
    }
    if (e->cell.w == 0 || e->cell.h == 0) {
        return;
    }
    FlRect quad = {
        glyph->x + e->left * run->scale,
        glyph->y - e->top * run->scale,
        (f32)e->cell.w * run->scale,
        (f32)e->cell.h * run->scale,
    };
    gpu_emit(
        tr, gpu_aabb_from_rect(quad),
        (GpuDrawCmd) {
            .rect = { quad.x, quad.y, quad.w, quad.h },
            .radius = { (f32)e->cell.x, (f32)e->cell.y, (f32)(e->cell.x + e->cell.w), (f32)(e->cell.y + e->cell.h) },
            .color = { tint },
            .kind = GpuDraw_Glyph,
        });
}

// A text run explodes to one GpuDraw_Glyph quad per glyph, resolved in the glyph-atlas bookkeeping by (run
// font, run size, glyph id).
void gpu_translate_text_run(GpuTranslate* tr, const void* payload) {
    const FlCmdTextRun* run = payload;
    if (tr->glyphs == nullptr) {
        return;
    }
    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    u32 tint = gpu_pack_tint_premultiplied(run->color, tr->global_alpha);
    for_count(i, run->glyph_count) {
        emit_glyph(tr, run, &glyphs[i], tint);
    }
}
