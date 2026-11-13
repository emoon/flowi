#include "gpu_ir_to_draw_cmd.h"
#include "gpu_draw_pack.h"
#include "gpu_image_slots.h"
#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_ir_records.h"
#include "gpu_svg_atlas.h"
#include "gpu_translate.h"
#include <core/arena.h>
#include <core/core.h>
#include <ui/render_backend.h>
#include <ui/render_ir.h>

// The full target rect: the root clip.
static FlRect target_rect(const GpuTranslate* tr) {
    GpuAabb target = gpu_clip_stack_root(&tr->stack);
    return (FlRect) { target.x0, target.y0, target.x1 - target.x0, target.y1 - target.y0 };
}

// One fullscreen background image, white-tinted at alpha, resolved through the normal image resolution. An invalid
// or unresolved handle emits nothing.
static void translate_background_image(GpuTranslate* tr, FlImageHandle handle, u8 alpha) {
    if (handle == FL_IMAGE_HANDLE_INVALID) {
        return;
    }
    FlCmdImage im = {
        .source = { .kind = FlImageKind_Static, .handle = handle },
        .mode = FlImageMode_Color,
        .dst = target_rect(tr),
        .src = { 0.0f, 0.0f, 0.0f, 0.0f }, // the whole image
        .tint = { 255, 255, 255, alpha },
        .filter = FlImageFilter_Linear,
    };
    gpu_translate_image(tr, &im);
}

// Clear (when opaque) then up to two background images, ahead of the IR - so background images claim sampler
// slots before any UI image. Background commands ignore the frame's global_alpha, so they emit at full opacity,
// modulated only by the background's own alpha / crossfade weight.
static void translate_background(GpuTranslate* tr) {
    const FlBackground* bg = tr->background;
    u8 saved_alpha = tr->global_alpha;
    const FlEffect* saved_effect = tr->effect;
    tr->global_alpha = 255; // the frame's global_alpha fades only the UI over the background
    tr->effect = NULL;      // background commands carry no effect annotation

    // Clear: a fullscreen rect in the clear color. A zero alpha leaves the target transparent.
    if (bg->clear.a > 0) {
        FlCmdFillRect clear = {
            .rect = target_rect(tr),
            .corner = { bg->clear, bg->clear, bg->clear, bg->clear },
        };
        gpu_translate_fill_rect(tr, &clear);
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

static void translate_clip_push(GpuTranslate* tr, const void* payload) {
    gpu_clip_stack_push(&tr->stack, payload);
}

static void translate_clip_pop(GpuTranslate* tr, const void* payload) {
    (void)payload;
    gpu_clip_stack_pop(&tr->stack);
}

typedef void (*TranslateFn)(GpuTranslate* tr, const void* payload);

// Kinds without an entry are skipped.
static const TranslateFn k_translators[] = {
    [FlCmd_FillRect] = gpu_translate_fill_rect,
    [FlCmd_Border] = gpu_translate_border,
    [FlCmd_TextRun] = gpu_translate_text_run,
    [FlCmd_Image] = gpu_translate_image,
    [FlCmd_Arc] = gpu_translate_arc,
    [FlCmd_Circle] = gpu_translate_circle,
    [FlCmd_Diamond] = gpu_translate_diamond,
    [FlCmd_Bar] = gpu_translate_bar_set,
    [FlCmd_Polyline] = gpu_translate_polyline,
    [FlCmd_ClipPush] = translate_clip_push,
    [FlCmd_ClipPop] = translate_clip_pop,
};

static void translate_record(void* user, const FlCmdHeader* header) {
    GpuTranslate* tr = user;
    // Read the effect before dispatch so the shared clip gate can expand bounds.
    const FlEffect* effect = gpu_record_effect(header);
    tr->effect = effect;
    u32 record_start = tr->n;
    if (header->kind < sizeof_array(k_translators) && k_translators[header->kind] != NULL) {
        k_translators[header->kind](tr, header + 1);
    }
    // Stamp the effect onto every command this record committed.
    if (effect != NULL) {
        for (u32 i = record_start; i < tr->n; i++) {
            gpu_pack_effect(&tr->out->cmds[i], effect, tr->global_alpha);
        }
    }
}

// One walk over the IR stream, filling the pre-sized draw list; the background emits ahead of the IR.
static void translate_stream(GpuTranslate* tr, const void* ir_commands, size_t ir_bytes) {
    if (tr->background != NULL) {
        translate_background(tr);
    }
    gpu_ir_walk(ir_commands, ir_bytes, translate_record, tr);
}

// Upper bound on the draw-command and clip arrays from record counts alone. Clips dedup to at most (pushes + 1)
// distinct rects (the root plus one per ClipPush). The arena is rewound each frame, so a loose bound costs only
// scratch space.
typedef struct GpuDrawBound {
    u32 cmd_max;  // bounds the cmds + aabbs arrays
    u32 clip_max; // bounds the clips array
} GpuDrawBound;

static void add_record_bound(void* user, const FlCmdHeader* header) {
    GpuDrawBound* bound = user;
    bound->cmd_max += gpu_record_cmd_bound(header);
    if (header->kind == FlCmd_ClipPush) {
        bound->clip_max++;
    }
}

static GpuDrawBound prescan_draw_bound(const void* ir_commands, size_t ir_bytes, bool has_background) {
    // The root clip, plus the background's clear fill and up to two crossfade images.
    GpuDrawBound bound = { .cmd_max = has_background ? 3u : 0u, .clip_max = 1u };
    gpu_ir_walk(ir_commands, ir_bytes, add_record_bound, &bound);
    return bound;
}

GpuDrawList gpu_ir_to_draw_cmds(FlArena* arena, const GpuIrInput* in) {
    // One translation pass into arrays pre-sized by a record-count pre-scan, filling [0, list.count) and
    // leaving any slack the loose bound allocated untouched (the consumer reads only list.count /
    // list.clip_count).
    GpuDrawBound bound = prescan_draw_bound(in->commands, in->bytes, in->background != NULL);

    GpuDrawList list = {
        .cmds = arena_alloc_array(arena, GpuDrawCmd, bound.cmd_max),
        .aabbs = arena_alloc_array(arena, GpuAabb, bound.cmd_max),
        .clips = arena_alloc_array(arena, GpuAabb, bound.clip_max),
        .count = 0,
    };
    list.clips[0] = (GpuAabb) { 0.0f, 0.0f, (f32)in->target_width, (f32)in->target_height };
    list.clip_count = 1;

    GpuTranslate tr = { .out = &list,
                        .atlas = in->atlas,
                        .glyphs = in->glyphs,
                        .svg = in->svg,
                        .svg_color = in->svg_color,
                        .slots = in->slots,
                        .global_alpha = in->global_alpha,
                        .background = in->background };
    gpu_clip_stack_init(&tr.stack, in->target_width, in->target_height);
    if (in->slots != NULL) {
        gpu_image_slots_frame_reset(in->slots);
    }
    translate_stream(&tr, in->commands, in->bytes);

    list.count = tr.n;
    return list;
}
