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
    FlArena* glyphs;
};

typedef struct ShapePen {
    kbts_shape_context* context;
    FlArena* arena;
    FlShapedRun out;
    FlFontId font;
    i64 units_per_em;
    i64 q4;
    i64 base;
    i64 units;
} ShapePen;

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
    self->glyphs = arena_new();
    return self;
}

void font_shaper_destroy(FontShaper* self) {
    kbts_DestroyShapeContext(self->context);
    fl_arena_destroy(self->glyphs);
}

static kbts_font* kb_font(FontShaperFont* font) {
    return font != nullptr ? &font->font : nullptr;
}

static i64 units_to_26_6(const ShapePen* pen, i64 units) {
    const i64 scaled = units * pen->q4 * 16;
    const i64 half = pen->units_per_em / 2;
    return (scaled >= 0 ? scaled + half : scaled - half) / pen->units_per_em;
}

// The glyph's codepoint index is the context's own; the user id given at shape time is the UTF-8 byte offset.
static u32 glyph_byte_offset(const ShapePen* pen, const kbts_glyph* glyph) {
    kbts_shape_codepoint codepoint = { 0 };
    kbts_ShapeGetShapeCodepoint(pen->context, glyph->UserIdOrCodepointIndex, &codepoint);
    return (u32)codepoint.UserId;
}

static void pen_emit_glyph(ShapePen* pen, const kbts_glyph* glyph) {
    FlShapedGlyph* out = arena_alloc_st(pen->arena, FlShapedGlyph);
    *out = (FlShapedGlyph) {
        .font = pen->font,
        .glyph_index = glyph->Id,
        .x = (i32)(pen->base + units_to_26_6(pen, pen->units + glyph->OffsetX)),
        .y = (i32)units_to_26_6(pen, glyph->OffsetY),
        .byte_offset = glyph_byte_offset(pen, glyph),
    };
    pen->out.count++;
    pen->units += glyph->Id != 0 ? glyph->AdvanceX : 0;
}

static void pen_emit_run(ShapePen* pen, const FontShaperFace* face, kbts_run* run) {
    pen->font = face->id;
    pen->units_per_em = face->font->units_per_em;
    kbts_glyph* glyph = nullptr;
    while (kbts_GlyphIteratorNext(&run->Glyphs, &glyph)) {
        pen_emit_glyph(pen, glyph);
    }
    pen->base += units_to_26_6(pen, pen->units);
    pen->units = 0;
}

static const FontShaperFace* run_face(const FontShaperRun* run, const kbts_font* font) {
    return font == kb_font(run->fallback.font) ? &run->fallback : &run->primary;
}

static void shape_text(kbts_shape_context* context, FlString text) {
    kbts_ShapeBegin(context, KBTS_DIRECTION_LTR, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context, text.data, (int)text.length, KBTS_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    kbts_ShapeEnd(context);
}

static FlShapedRun collect_runs(kbts_shape_context* context, const FontShaperRun* run, FlArena* arena) {
    ShapePen pen = { .context = context, .arena = arena, .q4 = run->size_q4 };
    pen.out.glyphs = arena_pos_type(arena, FlShapedGlyph);
    kbts_run shaped;
    while (kbts_ShapeRun(context, &shaped)) {
        pen_emit_run(&pen, run_face(run, shaped.Font), &shaped);
    }
    pen.out.width = pen.base;
    return pen.out;
}

static void replace_failed_context(FontShaper* self, FlFontId font, kbts_shape_error error) {
    log_error("kb could not shape text in font %u (error %d), the text is laid out unshaped", font, (int)error);
    kbts_DestroyShapeContext(self->context);
    self->context = kbts_CreateShapeContext(nullptr, nullptr);
}

bool font_shaper_shape(FontShaper* self, const FontShaperRun* run, FlString text, FlShapedRun* out) {
    kbts_shape_context* context = self->context;
    fl_arena_rewind(self->glyphs);
    kbts_ShapePushFont(context, kb_font(run->fallback.font));
    kbts_ShapePushFont(context, kb_font(run->primary.font));
    shape_text(context, text);
    *out = collect_runs(context, run, self->glyphs);

    const kbts_shape_error error = kbts_ShapeError(context);
    if (error != KBTS_SHAPE_ERROR_NONE) {
        replace_failed_context(self, run->primary.id, error);
        return false;
    }
    kbts_ShapePopFont(context);
    kbts_ShapePopFont(context);
    return true;
}
