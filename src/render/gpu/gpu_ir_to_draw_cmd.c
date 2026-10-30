#include "gpu_ir_to_draw_cmd.h"
#include <core/arena.h>
#include <core/log.h>
#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_svg_atlas.h"
#include "gpu_svg_translate.h"
#include <ui/render_backend.h>
#include <ui/render_ir.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const f32 GPU_TAU = 6.28318530717958647692f;

static bool is_renderable_rect(const FlCmdFillRect* fr) {
    return fr->rect.w > 0.0f && fr->rect.h > 0.0f;
}

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

// Clamp a corner radius to [0, half the smaller rect dimension]. Adjacent clamped radii can then never
// overlap: their sum is at most the smaller dimension, which is at most either side length.
static f32 clamp_radius(f32 radius, f32 max_radius) {
    return fminf(fmaxf(radius, 0.0f), max_radius);
}

// Border side widths and effect offsets/blur ride the packed u32 words bit-verbatim; the shader reads them
// back with uintBitsToFloat.
static u32 f32_bits(f32 v) {
    union {
        f32 f;
        u32 u;
    } pun = { .f = v };
    return pun.u;
}

// Pack a dash pattern into the flags word's free high bits (format in GPU_DRAW_DASH_*). Either length
// non-positive means solid, as does a pattern whose dash or gap rounds away to nothing at the 1/16px step.
static u32 pack_dash(FlStrokeStyle stroke) {
    if (!(stroke.dash_len > 0.0f) || !(stroke.gap_len > 0.0f)) {
        return 0u;
    }
    f32 max_len = (f32)GPU_DRAW_DASH_MASK / (f32)GPU_DRAW_DASH_UNITS_PER_PX;
    u32 dash = (u32)(fminf(stroke.dash_len, max_len) * (f32)GPU_DRAW_DASH_UNITS_PER_PX + 0.5f);
    u32 gap = (u32)(fminf(stroke.gap_len, max_len) * (f32)GPU_DRAW_DASH_UNITS_PER_PX + 0.5f);
    if (dash == 0u || gap == 0u) {
        return 0u;
    }
    return (dash << GPU_DRAW_DASH_SHIFT) | (gap << GPU_DRAW_GAP_SHIFT);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pack an FlColor with the frame's global_alpha folded into the alpha channel. Byte order matches GLSL
// unpackUnorm4x8: R in the low byte. The alpha product rounds to nearest so 255 * 255 stays 255.

static u32 pack_color(FlColor c, u8 global_alpha) {
    u32 a = ((u32)c.a * (u32)global_alpha + 127u) / 255u;
    return (u32)c.r | ((u32)c.g << 8) | ((u32)c.b << 16) | (a << 24);
}

// Image tints pack premultiplied (rgb * a) so the shader's modulation of the premultiplied atlas texel is a
// single componentwise multiply. The opaque white tint stays the exact identity.
static u32 pack_tint_premultiplied(FlColor tint, u8 global_alpha) {
    u32 a = ((u32)tint.a * (u32)global_alpha + 127u) / 255u;
    u32 r = ((u32)tint.r * a + 127u) / 255u;
    u32 g = ((u32)tint.g * a + 127u) / 255u;
    u32 b = ((u32)tint.b * a + 127u) / 255u;
    return r | (g << 8) | (b << 16) | (a << 24);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clip stack, walked over the single translation pass. Each level holds the effective clip - the running
// intersection down the stack, rooted at the full target - so the current clip is always the top and a pop is
// just a depth decrement.

enum { GPU_CLIP_STACK_DEPTH = 32 };

typedef struct GpuClipStack {
    GpuAabb effective[GPU_CLIP_STACK_DEPTH + 1]; // [0] = full target; [depth] = current
    u32 depth;
    u32 ignored_pushes; // pushes past capacity, matched against pops before the stack unwinds
    bool diagnosed;     // one diagnostic per frame, not one per record
} GpuClipStack;

static void clip_stack_init(GpuClipStack* s, u32 target_width, u32 target_height) {
    s->effective[0] = (GpuAabb) { 0.0f, 0.0f, (f32)target_width, (f32)target_height };
    s->depth = 0;
    s->ignored_pushes = 0;
    s->diagnosed = false;
}

static GpuAabb aabb_intersect(GpuAabb a, GpuAabb b) {
    return (GpuAabb) {
        .x0 = fmaxf(a.x0, b.x0),
        .y0 = fmaxf(a.y0, b.y0),
        .x1 = fminf(a.x1, b.x1),
        .y1 = fminf(a.y1, b.y1),
    };
}

static bool aabb_is_empty(GpuAabb a) {
    return a.x1 <= a.x0 || a.y1 <= a.y0;
}

static void clip_stack_push(GpuClipStack* s, const FlCmdClipPush* clip) {
    if (s->depth >= GPU_CLIP_STACK_DEPTH) {
        s->ignored_pushes++;
        if (!s->diagnosed) {
            log_error("gpu backend: clip stack deeper than %d - ignoring deeper pushes", GPU_CLIP_STACK_DEPTH);
            s->diagnosed = true;
        }
        return;
    }
    GpuAabb rect = { clip->rect.x, clip->rect.y, clip->rect.x + clip->rect.w, clip->rect.y + clip->rect.h };
    s->effective[s->depth + 1] = aabb_intersect(s->effective[s->depth], rect);
    s->depth++;
}

static void clip_stack_pop(GpuClipStack* s) {
    if (s->ignored_pushes > 0) {
        s->ignored_pushes--;
        return;
    }
    if (s->depth == 0) {
        if (!s->diagnosed) {
            log_error("gpu backend: clip pop on an empty stack - ignored");
            s->diagnosed = true;
        }
        return;
    }
    s->depth--;
}

static GpuAabb clip_stack_current(const GpuClipStack* s) {
    return s->effective[s->depth];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dedup lookup into the per-frame clip-rect array: exact float match (effective rects from identical push
// sequences are bit-identical).

static u32 clip_find_or_append(GpuAabb* clips, u32* clip_count, GpuAabb rect) {
    for (u32 i = 0; i < *clip_count; i++) {
        if (clips[i].x0 == rect.x0 && clips[i].y0 == rect.y0 && clips[i].x1 == rect.x1 && clips[i].y1 == rect.y1) {
            return i;
        }
    }
    clips[*clip_count] = rect;
    return (*clip_count)++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc geometry, shared by translation and the AABB pass. Records normalize once here (the shader never
// remaps angles).

typedef struct GpuArcShape {
    f32 cx;
    f32 cy;
    f32 outer;
    f32 inner;
    f32 start; // knob convention; sweep > 0 (clockwise) after normalization
    f32 sweep;
    bool round_caps;
    bool full;
} GpuArcShape;

static bool arc_shape(const FlCmdArc* arc, GpuArcShape* s) {
    if (arc->radius <= 0.0f || arc->thickness <= 0.0f || arc->sweep == 0.0f) {
        return false;
    }
    f32 start = arc->start_angle;
    f32 sweep = arc->sweep;
    if (sweep < 0.0f) {
        start += sweep;
        sweep = -sweep;
    }
    *s = (GpuArcShape) {
        .cx = arc->cx,
        .cy = arc->cy,
        .outer = arc->radius,
        .inner = fmaxf(arc->radius - arc->thickness, 0.0f),
        .start = start,
        .sweep = sweep,
        .round_caps = (arc->flags & FlArcFlags_round_caps) != 0,
        .full = sweep >= GPU_TAU,
    };
    return true;
}

// The point at a knob-convention angle: 0 at 12 o'clock, positive clockwise, y-down pixel space.
static void arc_point(const GpuArcShape* s, f32 angle, f32 r, f32* x, f32* y) {
    *x = s->cx + r * sinf(angle);
    *y = s->cy - r * cosf(angle);
}

static void aabb_add_point(GpuAabb* bb, f32 x, f32 y) {
    bb->x0 = fminf(bb->x0, x);
    bb->y0 = fminf(bb->y0, y);
    bb->x1 = fmaxf(bb->x1, x);
    bb->y1 = fmaxf(bb->y1, y);
}

// Sector-tight bounds, not the full circle: the extremes of {r in [inner, outer], angle in the sweep} are
// the endpoints at both radii plus the outer-radius cardinal directions the sweep crosses (interior maxima
// of sin/cos; inner-radius cardinals are never extreme - the outer one dominates outward and an endpoint
// dominates inward). Round caps add the cap discs at the centerline endpoints.
static GpuAabb arc_aabb(const GpuArcShape* s) {
    if (s->full) {
        return (GpuAabb) { s->cx - s->outer, s->cy - s->outer, s->cx + s->outer, s->cy + s->outer };
    }

    f32 x;
    f32 y;
    arc_point(s, s->start, s->outer, &x, &y);
    GpuAabb bb = { x, y, x, y };
    const f32 ends[2] = { s->start, s->start + s->sweep };
    for (int e = 0; e < 2; e++) {
        arc_point(s, ends[e], s->outer, &x, &y);
        aabb_add_point(&bb, x, y);
        arc_point(s, ends[e], s->inner, &x, &y);
        aabb_add_point(&bb, x, y);
        if (s->round_caps) {
            f32 cap_radius = 0.5f * (s->outer - s->inner);
            arc_point(s, ends[e], 0.5f * (s->outer + s->inner), &x, &y);
            aabb_add_point(&bb, x - cap_radius, y - cap_radius);
            aabb_add_point(&bb, x + cap_radius, y + cap_radius);
        }
    }

    // Cardinal crossings: normalize the start into [0, tau) and walk quarter turns across the sweep. The
    // sweep is below tau here (full circles returned above), so k stays under 8.
    f32 a0 = fmodf(s->start, GPU_TAU);
    if (a0 < 0.0f) {
        a0 += GPU_TAU;
    }
    const f32 quarter = 0.25f * GPU_TAU;
    for (int k = (int)ceilf(a0 / quarter); (f32)k * quarter <= a0 + s->sweep; k++) {
        switch (k & 3) {
            case 0:
                aabb_add_point(&bb, s->cx, s->cy - s->outer);
                break; // 12 o'clock
            case 1:
                aabb_add_point(&bb, s->cx + s->outer, s->cy);
                break; // 3 o'clock
            case 2:
                aabb_add_point(&bb, s->cx, s->cy + s->outer);
                break; // 6 o'clock
            case 3:
                aabb_add_point(&bb, s->cx - s->outer, s->cy);
                break; // 9 o'clock
        }
    }
    return bb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-record translation state. n is the running count of surviving commands and indexes the next free slot
// in out's pre-sized arrays.

typedef struct GpuTranslate {
    GpuDrawList* out;
    u32 n;
    GpuClipStack stack;
    const GpuImageAtlas* atlas;
    const GpuGlyphAtlas* glyphs;
    const GpuSvgAtlas* svg;       // monochrome R8 SVG coverage atlas
    const GpuSvgAtlas* svg_color; // multi-color RGBA8 SVG atlas
    GpuImageSlots* slots;
    u8 global_alpha;
    const FlEffect* effect;         // the current record's effect (NULL = none), set around each record's dispatch
    const FlBackground* background; // the frame's background state, emitted ahead of the IR (NULL = none)
} GpuTranslate;

// Locate a record's effect annotation, or NULL when it carries none. The effect is appended right after the
// payload and any inline trailing array - not at the record's byte_size, which is rounded up to the 8-byte
// record stride and so can leave trailing padding past the effect. A kind-0 (none) effect reports no effect; a
// kind whose payload size is not known here also reports none, but warns rather than dropping it silently.
static const FlEffect* record_effect(const FlCmdHeader* header) {
    if ((header->flags & FL_HAS_EFFECT) == 0) {
        return NULL;
    }
    const u8* payload = (const u8*)(header + 1);
    size_t payload_size;
    switch ((FlCmdKind)header->kind) {
        case FlCmd_FillRect:
            payload_size = sizeof(FlCmdFillRect);
            break;
        case FlCmd_Border:
            payload_size = sizeof(FlCmdBorder);
            break;
        case FlCmd_Image:
            payload_size = sizeof(FlCmdImage);
            break;
        case FlCmd_TextRun:
            payload_size = sizeof(FlCmdTextRun) + (size_t)((const FlCmdTextRun*)payload)->glyph_count * sizeof(FlGlyph);
            break;
        case FlCmd_Arc:
            payload_size = sizeof(FlCmdArc);
            break;
        case FlCmd_Circle:
            payload_size = sizeof(FlCmdCircle);
            break;
        case FlCmd_Diamond:
            payload_size = sizeof(FlCmdDiamond);
            break;
        case FlCmd_Bar:
            payload_size = sizeof(FlCmdBarSet) + (size_t)((const FlCmdBarSet*)payload)->bar_count * sizeof(FlBar);
            break;
        case FlCmd_Polyline:
            payload_size
                = sizeof(FlCmdPolyline) + (size_t)((const FlCmdPolyline*)payload)->point_count * sizeof(FlPolyPoint);
            break;
        default:
            log_warning("gpu translate: dropping the effect on command kind %u - no payload size for it",
                        (unsigned)header->kind);
            return NULL;
    }
    const FlEffect* e = (const FlEffect*)(payload + payload_size);
    return e->kind != FlEffectKind_None ? e : NULL;
}

// Expand a shape's bounds to cover where its effect reaches, before the bounds meet the clip: a drop shadow
// shifts by its offset and dilates by its blur, a glow stays centered and just dilates. The union with the
// unshifted shape keeps the primitive's own tiles.
static GpuAabb effect_expand_bounds(GpuAabb b, const FlEffect* e) {
    if (e == NULL || e->kind == FlEffectKind_None) {
        return b;
    }
    f32 blur = fmaxf(e->blur, 0.0f);
    f32 ox = e->kind == FlEffectKind_DropShadow ? e->off_x : 0.0f; // glow is centered
    f32 oy = e->kind == FlEffectKind_DropShadow ? e->off_y : 0.0f;
    GpuAabb reach = { b.x0 + ox - blur, b.y0 + oy - blur, b.x1 + ox + blur, b.y1 + oy + blur };
    return (GpuAabb) {
        .x0 = fminf(b.x0, reach.x0),
        .y0 = fminf(b.y0, reach.y0),
        .x1 = fmaxf(b.x1, reach.x1),
        .y1 = fmaxf(b.y1, reach.y1),
    };
}

// Stamp the current record's effect onto a committed command: the kind into the flags' effect field, the
// offset/blur as f32 bits and the alpha-folded color into the effect words.
static void effect_stamp(GpuDrawCmd* cmd, const FlEffect* e, u8 global_alpha) {
    cmd->flags |= ((u32)e->kind << GPU_DRAW_EFFECT_SHIFT) & GPU_DRAW_EFFECT_MASK;
    cmd->effect[0] = f32_bits(e->off_x);
    cmd->effect[1] = f32_bits(e->off_y);
    cmd->effect[2] = f32_bits(fmaxf(e->blur, 0.0f));
    cmd->effect[3] = pack_color(e->color, global_alpha);
}

// The survival gate every kind funnels through: a command whose clip-intersected bounds are empty is dropped
// entirely. A true return means the caller owns slot *slot - the AABB is stored and the caller must write
// cmds[*slot] with *clip_index.
static bool survive_clip(GpuTranslate* tr, GpuAabb bounds, u32* slot, u32* clip_index) {
    GpuAabb clip = clip_stack_current(&tr->stack);
    GpuAabb clipped = aabb_intersect(effect_expand_bounds(bounds, tr->effect), clip);
    if (aabb_is_empty(clipped)) {
        return false;
    }
    *slot = tr->n++;
    tr->out->aabbs[*slot] = clipped;
    *clip_index = clip_find_or_append(tr->out->clips, &tr->out->clip_count, clip);
    return true;
}

static void translate_fill_rect(GpuTranslate* tr, const FlCmdFillRect* fr) {
    u32 slot;
    u32 clip_index;
    GpuAabb bounds = { fr->rect.x, fr->rect.y, fr->rect.x + fr->rect.w, fr->rect.y + fr->rect.h };
    if (!is_renderable_rect(fr) || !survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    f32 max_radius = fminf(fr->rect.w, fr->rect.h) * 0.5f;
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { fr->rect.x, fr->rect.y, fr->rect.w, fr->rect.h },
        .radius = { clamp_radius(fr->radius.tl, max_radius), clamp_radius(fr->radius.tr, max_radius),
                    clamp_radius(fr->radius.bl, max_radius), clamp_radius(fr->radius.br, max_radius) },
        .color = { pack_color(fr->corner[0], tr->global_alpha), pack_color(fr->corner[1], tr->global_alpha),
                   pack_color(fr->corner[2], tr->global_alpha), pack_color(fr->corner[3], tr->global_alpha) },
        .kind = GpuDraw_Rect,
        .clip = clip_index,
    };
}

// Normalized here: negative widths clamp to 0, a record whose widths are all 0 (or whose rect is empty)
// drops, and widths consuming the rect (the inner rect vanishes) become a plain fill in the border color, so
// the shader's outer-minus-inner coverage split never degenerates.
static void translate_border(GpuTranslate* tr, const FlCmdBorder* b) {
    u32 slot;
    u32 clip_index;
    GpuAabb bounds = { b->rect.x, b->rect.y, b->rect.x + b->rect.w, b->rect.y + b->rect.h };
    f32 w_l = fmaxf(b->w_l, 0.0f);
    f32 w_r = fmaxf(b->w_r, 0.0f);
    f32 w_t = fmaxf(b->w_t, 0.0f);
    f32 w_b = fmaxf(b->w_b, 0.0f);
    bool renderable = b->rect.w > 0.0f && b->rect.h > 0.0f && w_l + w_r + w_t + w_b > 0.0f;
    if (!renderable || !survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    f32 max_radius = fminf(b->rect.w, b->rect.h) * 0.5f;
    f32 r_tl = clamp_radius(b->radius.tl, max_radius);
    f32 r_tr = clamp_radius(b->radius.tr, max_radius);
    f32 r_bl = clamp_radius(b->radius.bl, max_radius);
    f32 r_br = clamp_radius(b->radius.br, max_radius);
    u32 color = pack_color(b->color, tr->global_alpha);
    if (w_l + w_r >= b->rect.w || w_t + w_b >= b->rect.h) {
        tr->out->cmds[slot] = (GpuDrawCmd) {
            .rect = { b->rect.x, b->rect.y, b->rect.w, b->rect.h },
            .radius = { r_tl, r_tr, r_bl, r_br },
            .color = { color, color, color, color },
            .kind = GpuDraw_Rect,
            .clip = clip_index,
        };
        return;
    }
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { b->rect.x, b->rect.y, b->rect.w, b->rect.h },
        .radius = { r_tl, r_tr, r_bl, r_br },
        .color = { color, f32_bits(w_l), f32_bits(w_r), f32_bits(w_t) },
        .kind = GpuDraw_Border,
        .clip = clip_index,
        .flags = pack_dash(b->stroke),
        .aux = f32_bits(w_b),
    };
}

// Pre-test of the survival gate without committing a command slot: lets the image paths refuse to claim a
// sampler slot (or mark a dynamic upload) for a command that is fully clipped out.
static bool clip_visible(const GpuTranslate* tr, GpuAabb bounds) {
    return !aabb_is_empty(aabb_intersect(effect_expand_bounds(bounds, tr->effect), clip_stack_current(&tr->stack)));
}

// Emit a slot-image command (large static or dynamic source). The caller has already established clip
// visibility, so a granted slot is never wasted on an invisible command.
static void translate_slot_image(GpuTranslate* tr, const FlCmdImage* im, GpuAabb bounds, u32 image_index, u32 img_w,
                                 u32 img_h) {
    int s = gpu_image_slots_assign(tr->slots, image_index, im->filter);
    if (s < 0) {
        return; // every slot taken: the command drops, counted in slots->overflow
    }
    u32 slot;
    u32 clip_index;
    if (!survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    f32 src[4];
    image_src_rect(im, img_w, img_h, src);
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { im->dst.x, im->dst.y, im->dst.w, im->dst.h },
        .radius = { src[0], src[1], src[2], src[3] },
        .color = { pack_tint_premultiplied(im->tint, tr->global_alpha) },
        .kind = GpuDraw_SlotImage,
        .clip = clip_index,
        .aux = (u32)s,
    };
}

// Opaque white ({255,255,255,255}) is the image tint identity ("no modulation"). For a monochrome vector
// icon it is the signal to use the icon's authored fill; any other tint overrides it (recolor, like a glyph).
static bool tint_is_identity(FlColor c) {
    return c.r == 255 && c.g == 255 && c.b == 255 && c.a == 255;
}

// Emit a vector-icon command of the given kind: the handle resolved to its live SVG-atlas cell, tinted by the
// premultiplied tint with the frame global_alpha folded. gpu_svg_image_cmd forces a multi-color cell's tint to
// white (its colors are baked), so a record tint only recolors a monochrome icon.
static void translate_svg_cell(GpuTranslate* tr, const FlCmdImage* im, GpuAabb bounds, const GpuSvgEntry* svg,
                               GpuDrawKind kind, FlColor tint) {
    FlCmdImage routed = gpu_svg_image_cmd(im->source.handle, svg->cell, svg->cls, tint, im->dst);
    u32 slot;
    u32 clip_index;
    if (!survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { im->dst.x, im->dst.y, im->dst.w, im->dst.h },
        // The cell content sub-rect (padding inset) in SVG-atlas texels, x0 y0 x1 y1.
        .radius = { routed.src.x, routed.src.y, routed.src.x + routed.src.w, routed.src.y + routed.src.h },
        .color = { pack_tint_premultiplied(routed.tint, tr->global_alpha) },
        .kind = kind,
        .clip = clip_index,
    };
}

static void translate_image(GpuTranslate* tr, const FlCmdImage* im) {
    if (im->dst.w <= 0.0f || im->dst.h <= 0.0f) {
        return;
    }
    GpuAabb bounds = { im->dst.x, im->dst.y, im->dst.x + im->dst.w, im->dst.y + im->dst.h };

    if (im->source.kind == FlImageKind_Static) {
        // A handle is only ever one kind, so the resolution order is a fall-through, not a precedence.
        const GpuImageAtlasEntry* entry = tr->atlas != NULL ? gpu_image_atlas_find(tr->atlas, im->source.handle) : NULL;
        if (entry != NULL) {
            u32 slot;
            u32 clip_index;
            if (!survive_clip(tr, bounds, &slot, &clip_index)) {
                return;
            }
            f32 src[4];
            image_src_rect(im, entry->img_w, entry->img_h, src);
            tr->out->cmds[slot] = (GpuDrawCmd) {
                .rect = { im->dst.x, im->dst.y, im->dst.w, im->dst.h },
                .radius = { (f32)entry->cell.x + src[0], (f32)entry->cell.y + src[1], (f32)entry->cell.x + src[2],
                            (f32)entry->cell.y + src[3] },
                .color = { pack_tint_premultiplied(im->tint, tr->global_alpha) },
                .kind = GpuDraw_Image,
                .clip = clip_index,
                .flags = im->filter == FlImageFilter_Nearest ? GPU_DRAW_IMAGE_NEAREST : 0u,
            };
            return;
        }
        const GpuSvgEntry* svg = tr->svg != NULL ? gpu_svg_atlas_find(tr->svg, im->source.handle) : NULL;
        if (svg != NULL) {
            // Default tint is the icon's authored fill; a non-identity record tint overrides it.
            FlColor tint = tint_is_identity(im->tint) ? svg->mono_color : im->tint;
            translate_svg_cell(tr, im, bounds, svg, GpuDraw_Svg, tint);
            return;
        }
        const GpuSvgEntry* svg_color
            = tr->svg_color != NULL ? gpu_svg_atlas_find(tr->svg_color, im->source.handle) : NULL;
        if (svg_color != NULL) {
            translate_svg_cell(tr, im, bounds, svg_color, GpuDraw_SvgColor, im->tint);
            return;
        }
        int index = tr->slots != NULL ? gpu_image_slots_find_static(tr->slots, im->source.handle) : -1;
        if (index < 0 || !clip_visible(tr, bounds)) {
            return; // never prepared (or released), or fully clipped - no slot claimed
        }
        const GpuSlotImage* img = &tr->slots->images[index];
        translate_slot_image(tr, im, bounds, (u32)index, img->w, img->h);
        return;
    }

    // Dynamic source: touch marks the upload for this referencing frame - gated behind clip visibility so an
    // invisible command uploads nothing.
    if (tr->slots == NULL || im->source.dynamic.pixels == NULL || im->source.dynamic.w == 0 || im->source.dynamic.h == 0
        || !clip_visible(tr, bounds)) {
        return;
    }
    int index = gpu_image_slots_touch_dynamic(tr->slots, im->source.dynamic.pixels, im->source.dynamic.w,
                                              im->source.dynamic.h, im->source.dynamic.stride,
                                              im->mode == FlImageMode_AlphaCoverage);
    if (index < 0) {
        return; // table full - diagnosed by the slots module
    }
    translate_slot_image(tr, im, bounds, (u32)index, im->source.dynamic.w, im->source.dynamic.h);
}

// A text run explodes to one GpuDraw_Glyph quad per glyph, resolved in the glyph-atlas bookkeeping by (run
// font, run size, glyph id). The quad sits at the glyph's baseline pen position offset by the cell's left/top
// metrics (float pixels relative to the pen, y-up FreeType bearing): screen-left = pen.x + left, screen-top =
// pen.y - top, sized to the padded cell. radius carries the source texel rect in the R8 atlas; color[0] the
// run's tint premultiplied with global_alpha folded. A glyph absent from the atlas is skipped; an empty glyph
// (a metrics-only entry, w or h == 0, e.g. space) emits nothing.
static void translate_text_run(GpuTranslate* tr, const FlCmdTextRun* run) {
    if (tr->glyphs == NULL) {
        return;
    }
    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    u32 tint = pack_tint_premultiplied(run->color, tr->global_alpha);
    // The run is rasterized crisp at run->size (a quarter-px key; (run->size / 4) px) and displayed at that
    // size times run->scale: scale each glyph quad's bearing offset and extent by run->scale around the pen
    // position. The atlas source rect (radius[]) is left in raster texels, so the quad magnifies/minifies the
    // cell's coverage under the shader's linear sample.
    f32 scale = run->scale;
    for (u32 i = 0; i < run->glyph_count; i++) {
        const GpuGlyphEntry* e = gpu_glyph_atlas_find(tr->glyphs, run->font, run->size, glyphs[i].glyph_id);
        if (e == NULL || e->cell.w == 0 || e->cell.h == 0) {
            continue;
        }
        f32 rx = glyphs[i].x + e->left * scale;
        f32 ry = glyphs[i].y - e->top * scale;
        f32 rw = (f32)e->cell.w * scale;
        f32 rh = (f32)e->cell.h * scale;
        GpuAabb bounds = { rx, ry, rx + rw, ry + rh };
        u32 slot;
        u32 clip_index;
        if (!survive_clip(tr, bounds, &slot, &clip_index)) {
            continue;
        }
        tr->out->cmds[slot] = (GpuDrawCmd) {
            .rect = { rx, ry, rw, rh },
            .radius = { (f32)e->cell.x, (f32)e->cell.y, (f32)(e->cell.x + e->cell.w), (f32)(e->cell.y + e->cell.h) },
            .color = { tint },
            .kind = GpuDraw_Glyph,
            .clip = clip_index,
        };
    }
}

static void translate_arc(GpuTranslate* tr, const FlCmdArc* arc) {
    u32 slot;
    u32 clip_index;
    GpuArcShape s;
    if (!arc_shape(arc, &s) || !survive_clip(tr, arc_aabb(&s), &slot, &clip_index)) {
        return;
    }
    // The shader's wedge frame: rotate so the sweep's bisector points at 12 o'clock, cut at the half-aperture.
    f32 bisector = s.start + 0.5f * s.sweep;
    f32 aperture = 0.5f * s.sweep;
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { s.cx, s.cy, s.outer, s.inner },
        .radius = { cosf(bisector), sinf(bisector), sinf(aperture), cosf(aperture) },
        .color = { pack_color(arc->color, tr->global_alpha) },
        .kind = GpuDraw_Arc,
        .clip = clip_index,
        .flags = (s.round_caps ? GPU_DRAW_ARC_ROUND_CAPS : 0u) | (s.full ? GPU_DRAW_ARC_FULL : 0u),
    };
}

// Circle/diamond borders are an inward ring. Normalized here: a negative width to 0, and a width consuming
// the whole shape (>= the inradius) to a borderless shape in the border color.

static void translate_circle(GpuTranslate* tr, const FlCmdCircle* ci) {
    u32 slot;
    u32 clip_index;
    GpuAabb bounds = { ci->cx - ci->radius, ci->cy - ci->radius, ci->cx + ci->radius, ci->cy + ci->radius };
    if (ci->radius <= 0.0f || !survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    f32 border_width = fmaxf(ci->border_width, 0.0f);
    FlColor fill = ci->color;
    if (border_width >= ci->radius) {
        fill = ci->border_color;
        border_width = 0.0f;
    }
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { ci->cx, ci->cy, ci->radius, border_width },
        .color = { pack_color(fill, tr->global_alpha), pack_color(ci->border_color, tr->global_alpha) },
        .kind = GpuDraw_Circle,
        .clip = clip_index,
        // A dash needs a ring to sit on; a circle normalized down to a borderless disc has none.
        .flags = border_width > 0.0f ? pack_dash(ci->stroke) : 0u,
    };
}

static void translate_diamond(GpuTranslate* tr, const FlCmdDiamond* di) {
    u32 slot;
    u32 clip_index;
    GpuAabb bounds = { di->cx - di->rx, di->cy - di->ry, di->cx + di->rx, di->cy + di->ry };
    if (di->rx <= 0.0f || di->ry <= 0.0f || !survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    f32 border_width = fmaxf(di->border_width, 0.0f);
    FlColor fill = di->color;
    f32 inradius = di->rx * di->ry / sqrtf(di->rx * di->rx + di->ry * di->ry);
    if (border_width >= inradius) {
        fill = di->border_color;
        border_width = 0.0f;
    }
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { di->cx, di->cy, di->rx, di->ry },
        .radius = { border_width },
        .color = { pack_color(fill, tr->global_alpha), pack_color(di->border_color, tr->global_alpha) },
        .kind = GpuDraw_Diamond,
        .clip = clip_index,
    };
}

// Each bar of a set expands to one solid rect command off the shared baseline: a positive height extends up
// from the baseline, a negative one down. Degenerate bars drop bar-by-bar.
static void translate_bar_set(GpuTranslate* tr, const FlCmdBarSet* bs) {
    const FlBar* bars = (const FlBar*)(bs + 1);
    for (u32 b = 0; b < bs->bar_count; b++) {
        if (bars[b].w <= 0.0f || bars[b].h == 0.0f) {
            continue;
        }
        f32 y = bars[b].h >= 0.0f ? bs->baseline_y - bars[b].h : bs->baseline_y;
        f32 h = fabsf(bars[b].h);
        u32 slot;
        u32 clip_index;
        if (!survive_clip(tr, (GpuAabb) { bars[b].x, y, bars[b].x + bars[b].w, y + h }, &slot, &clip_index)) {
            continue;
        }
        u32 color = pack_color(bs->color, tr->global_alpha);
        tr->out->cmds[slot] = (GpuDrawCmd) {
            .rect = { bars[b].x, y, bars[b].w, h },
            .color = { color, color, color, color },
            .kind = GpuDraw_Rect,
            .clip = clip_index,
        };
    }
}

// One polyline segment - either a stroke capsule (p0..p1, round caps, half-width, per-endpoint intensities)
// or a fill-to-baseline trapezoid under the segment. The two share the rect = (p0.x, p0.y, p1.x, p1.y) and
// radius = (half_width, i0, i1, baseline) encoding; the stroke flag picks the shape and, in classification,
// whether the line-aware narrow phase runs. bounds is the segment's own kind-specific AABB.
static void emit_poly_segment(GpuTranslate* tr, GpuAabb bounds, f32 p0x, f32 p0y, f32 p1x, f32 p1y, f32 half_width,
                              f32 i0, f32 i1, f32 baseline_y, u32 packed_color, bool stroke) {
    u32 slot;
    u32 clip_index;
    if (!survive_clip(tr, bounds, &slot, &clip_index)) {
        return;
    }
    tr->out->cmds[slot] = (GpuDrawCmd) {
        .rect = { p0x, p0y, p1x, p1y },
        .radius = { half_width, i0, i1, baseline_y },
        .color = { packed_color },
        .kind = GpuDraw_Polyline,
        .clip = clip_index,
        .flags = stroke ? GPU_DRAW_POLY_STROKE : 0u,
    };
}

// Polyline: expand to one segment command per edge. A FILLED polyline emits its fill-to-baseline trapezoids
// first, then the stroke capsules, so the fill blends behind the stroke. Each stroke segment is a capsule
// with round caps/joins (the SDF's end discs); per-endpoint intensities lerp along it to modulate stroke
// alpha. A CLOSED polyline strokes the last->first edge too, with no fill trapezoid for it.
static void translate_polyline(GpuTranslate* tr, const FlCmdPolyline* pl) {
    if (pl->point_count < 2) {
        return;
    }
    const FlPolyPoint* pts = (const FlPolyPoint*)(pl + 1);
    f32 half_width = fmaxf(pl->width, 0.0f) * 0.5f;
    bool filled = (pl->flags & FlPolylineFlags_filled) != 0;
    bool closed = (pl->flags & FlPolylineFlags_closed) != 0;
    bool stroked = half_width > 0.0f;
    if (!stroked && !filled) {
        return;
    }
    u32 stroke_color = pack_color(pl->color, tr->global_alpha);
    u32 fill_color = pack_color(pl->fill_color, tr->global_alpha);
    f32 pad = half_width + 0.5f; // capsule reach plus the AA ramp

    // Fill trapezoids first (behind the stroke), one per open edge.
    if (filled) {
        for (u32 i = 0; i + 1 < pl->point_count; i++) {
            const FlPolyPoint* a = &pts[i];
            const FlPolyPoint* b = &pts[i + 1];
            if (a->x == b->x) {
                continue; // a vertical edge spans no x: its trapezoid is empty
            }
            GpuAabb bounds = {
                .x0 = fminf(a->x, b->x),
                .y0 = fminf(fminf(a->y, b->y), pl->baseline_y) - 0.5f,
                .x1 = fmaxf(a->x, b->x),
                .y1 = fmaxf(fmaxf(a->y, b->y), pl->baseline_y) + 0.5f,
            };
            emit_poly_segment(tr, bounds, a->x, a->y, b->x, b->y, 0.0f, a->intensity, b->intensity, pl->baseline_y,
                              fill_color, false);
        }
    }

    // Stroke capsules over the fill. The closing edge (CLOSED) is the last segment.
    if (stroked) {
        u32 seg_count = closed ? pl->point_count : pl->point_count - 1;
        for (u32 i = 0; i < seg_count; i++) {
            const FlPolyPoint* a = &pts[i];
            const FlPolyPoint* b = &pts[(i + 1) % pl->point_count];
            if (a->x == b->x && a->y == b->y) {
                continue; // a zero-length segment paints nothing (the round cap would be a lone dot)
            }
            GpuAabb bounds = {
                .x0 = fminf(a->x, b->x) - pad,
                .y0 = fminf(a->y, b->y) - pad,
                .x1 = fmaxf(a->x, b->x) + pad,
                .y1 = fmaxf(a->y, b->y) + pad,
            };
            emit_poly_segment(tr, bounds, a->x, a->y, b->x, b->y, half_width, a->intensity, b->intensity,
                              pl->baseline_y, stroke_color, true);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background: the frame envelope's background state translated to commands at the front of the stream (so the
// UI draws over it). Background commands ignore the frame's global_alpha, so they emit at full opacity,
// modulated only by the background's own alpha / crossfade weight.

// One fullscreen background image, white-tinted at alpha, resolved through the normal image resolution. An invalid
// or unresolved handle emits nothing.
static void translate_background_image(GpuTranslate* tr, FlImageHandle handle, u8 alpha) {
    if (handle == FL_IMAGE_HANDLE_INVALID) {
        return;
    }
    GpuAabb target = tr->stack.effective[0]; // the root clip is the full target rect
    FlCmdImage im = {
        .source = { .kind = FlImageKind_Static, .handle = handle },
        .mode = FlImageMode_Color,
        .dst = { target.x0, target.y0, target.x1 - target.x0, target.y1 - target.y0 },
        .src = { 0.0f, 0.0f, 0.0f, 0.0f }, // the whole image
        .tint = { 255, 255, 255, alpha },
        .filter = FlImageFilter_Linear,
    };
    translate_image(tr, &im);
}

// Clear (when opaque) then up to two background images, ahead of the IR - so background images claim sampler
// slots before any UI image.
static void translate_background(GpuTranslate* tr) {
    const FlBackground* bg = tr->background;
    u8 saved_alpha = tr->global_alpha;
    const FlEffect* saved_effect = tr->effect;
    tr->global_alpha = 255; // the frame's global_alpha fades only the UI over the background
    tr->effect = NULL;      // background commands carry no effect annotation

    // Clear: a fullscreen rect in the clear color. A zero alpha leaves the target transparent.
    if (bg->clear.a > 0) {
        GpuAabb target = tr->stack.effective[0];
        FlCmdFillRect clear = {
            .rect = { target.x0, target.y0, target.x1 - target.x0, target.y1 - target.y0 },
            .corner = { bg->clear, bg->clear, bg->clear, bg->clear },
        };
        translate_fill_rect(tr, &clear);
    }

    // Images over the clear. A valid prev signals a crossfade: prev opaque, current at crossfade_t, so the
    // over-composite is lerp(prev, current, t). Otherwise the single current image rides its own alpha.
    if (bg->prev != FL_IMAGE_HANDLE_INVALID && bg->current != FL_IMAGE_HANDLE_INVALID) {
        translate_background_image(tr, bg->prev, 255);
        translate_background_image(tr, bg->current, (u8)(bg->crossfade_t * 255.0f + 0.5f));
    } else {
        translate_background_image(tr, bg->current, bg->alpha);
    }

    tr->global_alpha = saved_alpha;
    tr->effect = saved_effect;
}

// One walk over the IR stream, filling the pre-sized draw list; the background emits ahead of the IR.
static void translate_stream(GpuTranslate* tr, const void* ir_commands, size_t ir_bytes) {
    if (tr->background != NULL) {
        translate_background(tr);
    }
    const u8* end = (const u8*)ir_commands + ir_bytes;
    for (const u8* p = (const u8*)ir_commands; p < end; p += ((const FlCmdHeader*)p)->byte_size) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        if (header->byte_size == 0) {
            break; // zero-size record
        }
        // Read the effect before dispatch so the shared clip gate can expand bounds.
        const FlEffect* effect = record_effect(header);
        tr->effect = effect;
        u32 record_start = tr->n;
        switch ((FlCmdKind)header->kind) {
            case FlCmd_FillRect:
                translate_fill_rect(tr, (const FlCmdFillRect*)(header + 1));
                break;
            case FlCmd_Border:
                translate_border(tr, (const FlCmdBorder*)(header + 1));
                break;
            case FlCmd_TextRun:
                translate_text_run(tr, (const FlCmdTextRun*)(header + 1));
                break;
            case FlCmd_Image:
                translate_image(tr, (const FlCmdImage*)(header + 1));
                break;
            case FlCmd_Arc:
                translate_arc(tr, (const FlCmdArc*)(header + 1));
                break;
            case FlCmd_Circle:
                translate_circle(tr, (const FlCmdCircle*)(header + 1));
                break;
            case FlCmd_Diamond:
                translate_diamond(tr, (const FlCmdDiamond*)(header + 1));
                break;
            case FlCmd_Bar:
                translate_bar_set(tr, (const FlCmdBarSet*)(header + 1));
                break;
            case FlCmd_Polyline:
                translate_polyline(tr, (const FlCmdPolyline*)(header + 1));
                break;
            case FlCmd_ClipPush:
                clip_stack_push(&tr->stack, (const FlCmdClipPush*)(header + 1));
                break;
            case FlCmd_ClipPop:
                clip_stack_pop(&tr->stack);
                break;
            default:
                break;
        }
        // Stamp the effect onto every command this record committed.
        if (effect != NULL) {
            for (u32 i = record_start; i < tr->n; i++) {
                effect_stamp(&tr->out->cmds[i], effect, tr->global_alpha);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upper bound on the draw-command and clip arrays from record counts alone. Each kind's bound is the most
// commands it can ever commit: the fixed-shape kinds one each, a text run one quad per glyph, a bar set one
// rect per bar, a polyline up to one fill trapezoid per open edge plus one stroke capsule per edge
// (<= 2 * point_count). The background, when present, can emit a clear fill plus up to two crossfade images.
// Clips dedup to at most (pushes + 1) distinct rects (the root plus one per ClipPush). The arena is rewound
// each frame, so a loose bound costs only scratch space.

typedef struct GpuDrawBound {
    u32 cmd_max;  // bounds the cmds + aabbs arrays
    u32 clip_max; // bounds the clips array
} GpuDrawBound;

static GpuDrawBound prescan_draw_bound(const void* ir_commands, size_t ir_bytes, bool has_background) {
    u32 cmd_max = has_background ? 3u : 0u; // clear fill + up to two crossfade images
    u32 push_count = 0;
    const u8* end = (const u8*)ir_commands + ir_bytes;
    for (const u8* p = (const u8*)ir_commands; p < end; p += ((const FlCmdHeader*)p)->byte_size) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        if (header->byte_size == 0) {
            break; // zero-size record
        }
        const void* payload = header + 1;
        switch ((FlCmdKind)header->kind) {
            case FlCmd_TextRun:
                cmd_max += ((const FlCmdTextRun*)payload)->glyph_count;
                break;
            case FlCmd_Bar:
                cmd_max += ((const FlCmdBarSet*)payload)->bar_count;
                break;
            case FlCmd_Polyline:
                cmd_max += 2u * ((const FlCmdPolyline*)payload)->point_count;
                break;
            case FlCmd_ClipPush:
                push_count++;
                break;
            case FlCmd_FillRect:
            case FlCmd_Border:
            case FlCmd_Image:
            case FlCmd_Arc:
            case FlCmd_Circle:
            case FlCmd_Diamond:
                cmd_max += 1u;
                break;
            case FlCmd_ClipPop:
            default:
                break;
        }
    }
    return (GpuDrawBound) { .cmd_max = cmd_max, .clip_max = push_count + 1u };
}

GpuDrawList gpu_ir_to_draw_cmds(FlArena* arena, const void* ir_commands, size_t ir_bytes, u8 global_alpha,
                                u32 target_width, u32 target_height, const GpuImageAtlas* atlas,
                                const GpuGlyphAtlas* glyphs, GpuImageSlots* slots, const FlBackground* background,
                                const GpuSvgAtlas* svg, const GpuSvgAtlas* svg_color) {
    // One translation pass into arrays pre-sized by a record-count pre-scan, filling [0, list.count) and
    // leaving any slack the loose bound allocated untouched (the consumer reads only list.count /
    // list.clip_count).
    GpuDrawBound bound = prescan_draw_bound(ir_commands, ir_bytes, background != NULL);

    GpuDrawList list = {
        .cmds = arena_alloc_array(arena, GpuDrawCmd, bound.cmd_max),
        .aabbs = arena_alloc_array(arena, GpuAabb, bound.cmd_max),
        .clips = arena_alloc_array(arena, GpuAabb, bound.clip_max),
        .count = 0,
    };
    list.clips[0] = (GpuAabb) { 0.0f, 0.0f, (f32)target_width, (f32)target_height };
    list.clip_count = 1;

    GpuTranslate tr = { .out = &list,
                        .atlas = atlas,
                        .glyphs = glyphs,
                        .svg = svg,
                        .svg_color = svg_color,
                        .slots = slots,
                        .global_alpha = global_alpha,
                        .background = background };
    clip_stack_init(&tr.stack, target_width, target_height);
    if (slots != NULL) {
        gpu_image_slots_frame_reset(slots);
    }
    translate_stream(&tr, ir_commands, ir_bytes);

    list.count = tr.n;
    return list;
}
