#include "shaped_run_cache.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <string.h>

#define BUCKET_COUNT 8192

typedef struct Entry {
    FlFontId font;
    FlFontId fallback;
    u16 size_q4;
    FlString text;
    FlShapedRun shaped;
} Entry;

#define ENTRY_BYTES (sizeof(HashMapNodeBase) + sizeof(u64) + sizeof(Entry))

struct ShapedRunCache {
    FlArena* arena;
    hashmap(u64, Entry) entries;
    u64 bytes;
    u64 byte_limit;
    StatsRegistry* stats;
    u32 misses;
    u32 clears;
};

ShapedRunCache* shaped_run_cache_new(FlArena* arena, u64 byte_limit, StatsRegistry* stats) {
    const FlString category = FL_S("shaped_run_cache");
    ShapedRunCache* self = arena_alloc_zero(arena, ShapedRunCache);
    self->arena = arena_new();
    self->byte_limit = byte_limit;
    self->stats = stats;
    self->misses = stats_register_counter(stats, FL_S("misses"), category);
    self->clears = stats_register_counter(stats, FL_S("clears"), category);
    shaped_run_cache_clear(self);
    return self;
}

void shaped_run_cache_destroy(ShapedRunCache* self) {
    fl_arena_destroy(self->arena);
}

void shaped_run_cache_clear(ShapedRunCache* self) {
    fl_arena_rewind(self->arena);
    hashmap_new(&self->entries, self->arena, BUCKET_COUNT);
    self->bytes = 0;
}

static u64 entry_hash(const FontShaperRun* run, FlString text) {
    const u64 fonts = (u64)run->primary.id << 32 | (u64)run->fallback.id << 16 | run->size_q4;
    return hashmap_hash(text.data, text.length) ^ fonts;
}

static bool same_text(FlString a, FlString b) {
    return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}

static bool entry_matches(const Entry* entry, const FontShaperRun* run, FlString text) {
    const bool fonts = entry->font == run->primary.id && entry->fallback == run->fallback.id;
    return fonts && entry->size_q4 == run->size_q4 && same_text(entry->text, text);
}

bool shaped_run_cache_find(ShapedRunCache* self, const FontShaperRun* run, FlString text, FlShapedRun* out) {
    const Entry* entry = hashmap_get(&self->entries, entry_hash(run, text));
    if (entry == nullptr || !entry_matches(entry, run, text)) {
        return false;
    }
    *out = entry->shaped;
    return true;
}

FlShapedRun shaped_run_cache_store(ShapedRunCache* self, const FontShaperRun* run, FlString text, FlShapedRun shaped) {
    const u64 glyph_bytes = shaped.count * sizeof(FlShapedGlyph);
    const u64 bytes = ENTRY_BYTES + text.length + glyph_bytes;
    stats_inc(self->stats, self->misses);
    if (bytes > self->byte_limit) {
        return shaped;
    }
    if (self->bytes + bytes > self->byte_limit) {
        stats_inc(self->stats, self->clears);
        shaped_run_cache_clear(self);
    }
    self->bytes += bytes;

    char* text_copy = arena_alloc_array(self->arena, char, text.length);
    FlShapedGlyph* glyphs = arena_alloc_array(self->arena, FlShapedGlyph, shaped.count);
    memcpy(text_copy, text.data, text.length);
    memcpy(glyphs, shaped.glyphs, glyph_bytes);

    const Entry entry = {
        .font = run->primary.id,
        .fallback = run->fallback.id,
        .size_q4 = run->size_q4,
        .text = { .data = text_copy, .length = text.length },
        .shaped = { .glyphs = glyphs, .count = shaped.count, .width = shaped.width },
    };
    hashmap_insert(&self->entries, entry_hash(run, text), entry);
    return entry.shaped;
}
