#include "font_shaper.h"
#include <core/arena.h>
#include <core/heap.h>
#include <core/log.h>
#include <kb/kb_text_shape.h>
#include <limits.h>
#include <stddef.h>

struct FontShaperFont {
    kbts_font font;
    u16 units_per_em;
};

struct FontShaper {
    kbts_shape_context* context;
    FlArena* scratch;
};

typedef struct Collector {
    FlArena* arena;
    FontUnitRun run;
    bool in_range;
} Collector;

typedef struct PlacePen {
    const FontShaperRun* run;
    i64 base;
    i64 units;
    u8 face;
} PlacePen;

static u16 font_units_per_em(kbts_font* font) {
    kbts_font_info2_1 info = { .Base.Size = sizeof(info) };
    kbts_GetFontInfo2(font, &info.Base);
    return info.UnitsPerEm;
}

static bool is_loadable(const u8* data, i64 size) {
    return data != nullptr && size > 0 && size <= INT_MAX;
}

FontShaperFont* font_shaper_font_new(FlArena* arena, const u8* data, i64 size) {
    if (!is_loadable(data, size)) {
        return nullptr;
    }

    FontShaperFont* out = arena_alloc_zero(arena, FontShaperFont);
    kbts_load_font_state state = { 0 };
    int scratch_size = 0;
    int output_size = 0;
    const kbts_load_font_error error
        = kbts_LoadFont(&out->font, &state, (void*)data, (int)size, 0, &scratch_size, &output_size);
    if (error == KBTS_LOAD_FONT_ERROR_NEED_TO_CREATE_BLOB) {
        void* scratch = fl_heap_alloc((u64)scratch_size);
        void* output = fl_arena_alloc_raw(arena, (u64)output_size, _Alignof(max_align_t));
        kbts_PlaceBlob(&out->font, &state, scratch, output);
        fl_heap_free(scratch);
    }
    if (!kbts_FontIsValid(&out->font)) {
        return nullptr;
    }
    out->units_per_em = font_units_per_em(&out->font);
    return out->units_per_em != 0 ? out : nullptr;
}

FontShaper* font_shaper_new(FlArena* arena) {
    FontShaper* self = arena_alloc_zero(arena, FontShaper);
    self->context = kbts_CreateShapeContext(nullptr, nullptr);
    self->scratch = arena_new();
    return self;
}

void font_shaper_destroy(FontShaper* self) {
    kbts_DestroyShapeContext(self->context);
    fl_arena_destroy(self->scratch);
}

static kbts_font* kb_font(FontShaperFont* font) {
    return font != nullptr ? &font->font : nullptr;
}

static bool fits_i16(int value) {
    return value >= INT16_MIN && value <= INT16_MAX;
}

static bool glyph_fits(const kbts_glyph* glyph, int advance) {
    const bool offset = fits_i16(glyph->OffsetX) && fits_i16(glyph->OffsetY);
    return offset && fits_i16(advance);
}

static void collect_glyph(Collector* collector, u8 face, const kbts_glyph* glyph) {
    const int advance = glyph->Id != 0 ? glyph->AdvanceX : 0;
    FontUnitGlyph* out = arena_alloc_st(collector->arena, FontUnitGlyph);
    *out = (FontUnitGlyph) {
        .glyph_index = glyph->Id,
        .advance = (i16)advance,
        .offset_x = (i16)glyph->OffsetX,
        .offset_y = (i16)glyph->OffsetY,
        .face = face,
    };
    collector->run.count++;
    collector->run.width_units += advance;
    collector->run.has_offsets |= glyph->OffsetX != 0 || glyph->OffsetY != 0;
    collector->run.has_faces |= face != 0;
    collector->in_range &= glyph_fits(glyph, advance);
}

static void collect_run(Collector* collector, u8 face, kbts_run* run) {
    kbts_glyph* glyph = nullptr;
    while (kbts_GlyphIteratorNext(&run->Glyphs, &glyph)) {
        collect_glyph(collector, face, glyph);
    }
}

static u8 run_face(const FontShaperRun* run, const kbts_font* font) {
    return font == kb_font(run->fallback.font) ? 1 : 0;
}

static void shape_text(kbts_shape_context* context, FlString text) {
    kbts_ShapeBegin(context, KBTS_DIRECTION_LTR, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context, text.data, (int)text.length, KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(context);
}

static Collector collect_runs(FontShaper* self, const FontShaperRun* run) {
    Collector collector = { .arena = self->scratch, .in_range = true };
    collector.run.glyphs = arena_pos_type(collector.arena, FontUnitGlyph);
    kbts_run shaped;
    while (kbts_ShapeRun(self->context, &shaped)) {
        collect_run(&collector, run_face(run, shaped.Font), &shaped);
    }
    return collector;
}

static void replace_failed_context(FontShaper* self, FlFontId font, kbts_shape_error error) {
    log_error("kb could not shape text in font %u (error %d), the text is laid out unshaped", font, (int)error);
    kbts_DestroyShapeContext(self->context);
    self->context = kbts_CreateShapeContext(nullptr, nullptr);
}

bool font_shaper_shape(FontShaper* self, const FontShaperRun* run, FlString text, FontUnitRun* out) {
    kbts_shape_context* context = self->context;
    fl_arena_rewind(self->scratch);
    kbts_ShapePushFont(context, kb_font(run->fallback.font));
    kbts_ShapePushFont(context, kb_font(run->primary.font));
    shape_text(context, text);
    const Collector collector = collect_runs(self, run);

    const kbts_shape_error error = kbts_ShapeError(context);
    if (error != KBTS_SHAPE_ERROR_NONE) {
        replace_failed_context(self, run->primary.id, error);
        return false;
    }
    kbts_ShapePopFont(context);
    kbts_ShapePopFont(context);
    if (!collector.in_range) {
        log_error("font %u has a glyph metric kb text cannot store, the text is laid out unshaped", run->primary.id);
        return false;
    }
    *out = collector.run;
    return true;
}

static const FontShaperFace* pen_face(const PlacePen* pen) {
    return pen->face != 0 ? &pen->run->fallback : &pen->run->primary;
}

static i64 pen_scale(const PlacePen* pen, i64 units) {
    const i64 units_per_em = pen_face(pen)->font->units_per_em;
    const i64 scaled = units * pen->run->size_q4 * 16;
    const i64 half = units_per_em / 2;
    return (scaled >= 0 ? scaled + half : scaled - half) / units_per_em;
}

static void pen_set_face(PlacePen* pen, u8 face) {
    if (face == pen->face) {
        return;
    }
    pen->base += pen_scale(pen, pen->units);
    pen->units = 0;
    pen->face = face;
}

static FlShapedGlyph pen_glyph(const PlacePen* pen, const FontUnitGlyph* glyph) {
    return (FlShapedGlyph) {
        .font = pen_face(pen)->id,
        .glyph_index = glyph->glyph_index,
        .x = (i32)(pen->base + pen_scale(pen, pen->units + glyph->offset_x)),
        .y = (i32)pen_scale(pen, glyph->offset_y),
    };
}

static i64 place_glyphs(PlacePen* pen, const FontUnitRun* units, FlShapedGlyph* glyphs) {
    for_count(i, units->count) {
        const FontUnitGlyph* glyph = &units->glyphs[i];
        pen_set_face(pen, glyph->face);
        if (glyphs != nullptr) {
            glyphs[i] = pen_glyph(pen, glyph);
        }
        pen->units += glyph->advance;
    }
    return pen->base + pen_scale(pen, pen->units);
}

void font_shaper_place(const FontUnitRun* units, const FontShaperRun* run, FlShapedRun* out) {
    PlacePen pen = { .run = run };
    *out = (FlShapedRun) { .count = units->count };
    if (run->glyph_arena == nullptr && !units->has_faces) {
        out->width = pen_scale(&pen, units->width_units);
        return;
    }
    FlShapedGlyph* glyphs = nullptr;
    if (run->glyph_arena != nullptr) {
        glyphs = arena_alloc_array_st(run->glyph_arena, FlShapedGlyph, units->count);
    }
    out->glyphs = glyphs;
    out->width = place_glyphs(&pen, units, glyphs);
}
