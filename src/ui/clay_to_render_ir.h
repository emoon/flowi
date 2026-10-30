#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clay render output -> neutral render-command IR (render_ir.h).
//
// Pure intent extraction: colors stay raw (no premultiply, no global_alpha fold), rounded rects keep their radii
// unsplit, borders keep per-side widths, and images/text reference resources by handle/id.

struct FlArena;
struct FlFontCache;
struct Clay_RenderCommand;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Append IR records for the given Clay commands to arena. scratch holds transient glyph layout for text
// runs; font_cache resolves glyph metrics/advances for text positioning.

void fl_clay_to_render_ir(struct FlArena* arena, struct FlArena* scratch, struct FlFontCache* font_cache,
                          struct Clay_RenderCommand* clay_commands, int clay_count);
