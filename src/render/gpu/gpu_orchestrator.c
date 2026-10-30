// Shared GPU frame orchestrator: the canonical per-frame sequence both backends drive, plus the IR-driven
// glyph-realization walk that feeds it. Backend-agnostic - no GL or Metal type appears here; every
// device-specific step goes through the GpuRenderOps table (gpu_orchestrator.h).

#include "gpu_orchestrator.h"
#include <core/arena.h>
#include <ui/render_backend.h>
#include <ui/font_cache.h>
#include <ui/render_ir.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Realize every glyph the frame's text runs reference into the coverage atlas, pulling outlines from the bound
// font cache and handing each to the backend's prepare_glyph seam. prepare_glyph is idempotent, so a resident
// glyph short-circuits and re-running every frame also repopulates the atlas after a deferred overflow wipe.
// With no font cache bound the seam is left to the host.

static void gpu_prepare_text_glyphs(const GpuRenderOps* ops, void* backend, const struct FlRenderFrame* frame) {
    struct FlFontCache* font_cache = ops->get_font_cache(backend);
    if (font_cache == NULL || frame == NULL) {
        return;
    }

    const u8* p = (const u8*)frame->commands;
    const u8* end = p + frame->bytes;
    while (p < end) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        if (header->byte_size == 0) {
            break; // zero-size record
        }
        if (header->kind == FlCmd_TextRun) {
            const FlCmdTextRun* run = (const FlCmdTextRun*)(header + 1);
            const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
            for (u32 i = 0; i < run->glyph_count; i++) {
                u32 codepoint = glyphs[i].glyph_id; // the IR keys glyphs by codepoint (ui/render_ir.h: FlGlyph)
                FlTempArena scratch = fl_arena_scratch_begin();
                // A codepoint the run's font lacks resolves to the fallback face - otherwise the glyph occupies
                // its fallback advance width but rasterizes as .notdef.
                FontGlyphRef glyph = font_cache_resolve_glyph(font_cache, run->font, codepoint);
                FlGlyphOutline outline;
                if (font_cache_get_glyph_outline(font_cache, glyph.font, run->size, glyph.glyph_index, scratch.arena,
                                                 &outline)) {
                    // Atlas key is the RUN's font + codepoint + raster size - the same key the translator looks
                    // up - regardless of which face supplied the outline.
                    ops->prepare_glyph(backend, run->font, run->size, codepoint, &outline);
                }
                fl_arena_scratch_end(scratch);
            }
        }
        p += header->byte_size;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_orchestrate_frame(const GpuRenderOps* ops, void* backend, void* surface, const struct FlRenderFrame* frame,
                           u32 screen_w, u32 screen_h, GpuGlyphAtlas* glyph_atlas, FlArena* frame_arena,
                           FlArena* perm_arena) {
    // Per-frame preamble, then realize the frame's glyphs before translation resolves them to atlas cells.
    ops->frame_begin(backend);
    gpu_prepare_text_glyphs(ops, backend, frame);

    // Translate the neutral IR into the backend's draw-command / AABB / clip arrays.
    GpuDrawList list = ops->translate(backend, frame, screen_w, screen_h);

    // The two-dispatch all-compute pipeline: classification (one thread per tile) writes each tile's
    // command list, then raster (one workgroup per tile) blends the classified commands into the offscreen
    // target. An empty frame still runs both passes - classification zeroes every tile count and raster clears
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
