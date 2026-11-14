// Shared GPU frame orchestrator: the canonical per-frame sequence both backends drive, plus the IR-driven
// glyph-realization walk that feeds it. Backend-agnostic: no GL or Metal type appears here; every
// device-specific step goes through the GpuRenderOps table (gpu_orchestrator.h).

#include "gpu_orchestrator.h"
#include <core/arena.h>
#include <ui/render_backend.h>
#include <ui/font_cache.h>
#include <ui/render_ir.h>

static bool gpu_font_is_builtin(struct FlFontCache* font_cache, FlFontId font) {
    FlFontRasterSource source;
    return font_cache_get_raster_source(font_cache, font, &source) && source.kind == FlFontRasterKind_Default;
}

static FontGlyphRef gpu_outline_glyph(struct FlFontCache* font_cache, FlFontId font, bool builtin, u32 glyph_index) {
    if (builtin) {
        return font_cache_resolve_glyph(font_cache, font, glyph_index);
    }
    return (FontGlyphRef) { .font = font, .glyph_index = glyph_index };
}

static void gpu_prepare_run_glyphs(const GpuRenderOps* ops, void* backend, struct FlFontCache* font_cache,
                                   const FlCmdTextRun* run) {
    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    const bool builtin = gpu_font_is_builtin(font_cache, run->font);
    for_count(i, run->glyph_count) {
        const u32 glyph_index = glyphs[i].glyph_index;
        const FontGlyphRef glyph = gpu_outline_glyph(font_cache, run->font, builtin, glyph_index);
        FlTempArena scratch = fl_arena_scratch_begin();
        FlGlyphOutline outline;
        if (font_cache_get_glyph_outline(font_cache, glyph.font, run->size, glyph.glyph_index, scratch.arena,
                                         &outline)) {
            // The atlas key is the run's font + glyph index + raster size (the key the translator looks up).
            ops->prepare_glyph(backend, run->font, run->size, glyph_index, &outline);
        }
        fl_arena_scratch_end(scratch);
    }
}

// Realize every glyph the frame's text runs reference into the coverage atlas, pulling outlines from the bound
// font cache and handing each to the backend's prepare_glyph seam. prepare_glyph is idempotent, so a resident
// glyph short-circuits and re-running every frame also repopulates the atlas after a deferred overflow wipe.
// With no font cache bound the seam is left to the host.

static void gpu_prepare_text_glyphs(const GpuRenderOps* ops, void* backend, const struct FlRenderFrame* frame) {
    struct FlFontCache* font_cache = ops->get_font_cache(backend);
    if (font_cache == nullptr || frame == nullptr) {
        return;
    }

    const u8* p = (const u8*)frame->commands;
    const u8* end = p + frame->bytes;
    while (p < end) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        if (header->byte_size == 0) {
            break;
        }
        if (header->kind == FlCmd_TextRun) {
            gpu_prepare_run_glyphs(ops, backend, font_cache, (const FlCmdTextRun*)(header + 1));
        }
        p += header->byte_size;
    }
}

void gpu_orchestrate_frame(const GpuRenderOps* ops, void* backend, void* surface, const struct FlRenderFrame* frame,
                           u32 screen_w, u32 screen_h, GpuGlyphAtlas* glyph_atlas, FlArena* frame_arena,
                           FlArena* perm_arena) {
    // Per-frame preamble, then realize the frame's glyphs before translation resolves them to atlas cells.
    ops->frame_begin(backend);
    gpu_prepare_text_glyphs(ops, backend, frame);

    GpuDrawList list = ops->translate(backend, frame, screen_w, screen_h);

    // The two-dispatch all-compute pipeline: classification (one thread per tile) writes each tile's
    // command list, then raster (one workgroup per tile) blends the classified commands into the offscreen
    // target. An empty frame still runs both passes: classification zeroes every tile count and raster clears
    // the target to transparent.
    ops->begin_encoder(backend, surface);
    ops->upload_inputs(backend, &list);
    ops->classify(backend, surface, list.count);
    ops->barrier(backend);
    ops->raster(backend, surface);
    ops->end_encoder(backend);

    // Honor a glyph-atlas overflow flagged during this frame's prepare pass. The frame boundary is the only
    // backend-controlled point between this frame's draw and the next prepare pass, so wiping here cannot blank a
    // glyph already drawn this frame.
    gpu_glyph_atlas_wipe_if_needed(glyph_atlas, perm_arena);

    fl_arena_rewind(frame_arena); // the translation scratch (list.cmds/aabbs/clips) is consumed
}
