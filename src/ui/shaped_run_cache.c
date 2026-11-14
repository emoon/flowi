#include "shaped_run_cache.h"
#include <core/arena.h>
#include <core/assert.h>
#include <core/hashmap.h>
#include <core/tlsf.h>
#include <string.h>

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

#define MAX_TEXT_BYTES 1024
#define MAX_ENTRIES 32768
#define BUCKET_COUNT 16384
#define IDLE_FRAMES 120
#define NO_BLOCK UINT32_MAX

enum {
    Entry_Offsets = 1 << 0,
    Entry_Faces = 1 << 1,
    Entry_Unshaped = 1 << 2,
};

typedef struct PackedGlyph {
    u16 glyph_index;
    i16 advance;
} PackedGlyph;

typedef struct PackedOffset {
    i16 x;
    i16 y;
} PackedOffset;

typedef struct Sections {
    PackedGlyph* glyphs;
    PackedOffset* offsets;
    u8* faces;
} Sections;

typedef struct ShapedRunHot {
    u32 last_frame;
    i32 width_units;
    u32 block;
    u16 glyph_count;
    u16 flags;
} ShapedRunHot;

typedef struct ShapedRunEntry {
    ShapedRunHot hot;
    u64 hash_high;
} ShapedRunEntry;

_Static_assert(sizeof(ShapedRunHot) == 16, "the hot record is 16 bytes");

typedef struct RunKey {
    u64 low;
    u64 high;
} RunKey;

typedef struct KeyContext {
    FlFontId primary;
    FlFontId fallback;
    u64 length;
} KeyContext;

typedef struct CounterIds {
    u32 hits;
    u32 misses;
    u32 evictions;
    u32 bytes_used;
} CounterIds;

struct ShapedRunCache {
    FlArena* arena;
    FlArena* unpacked;
    FontShaper* shaper;
    hashmap(u64, ShapedRunEntry) entries;
    u64* victims;
    u8* pool_memory;
    u64 pool_bytes;
    tlsf_t pool;
    u64 bytes_used;
    u32 frame;
    StatsRegistry* stats;
    CounterIds ids;
};

static CounterIds register_counters(StatsRegistry* stats) {
    if (stats == nullptr) {
        return (CounterIds) { 0 };
    }
    const FlString category = FL_S("shaped_run_cache");
    return (CounterIds) {
        .hits = stats_register_counter(stats, FL_S("hits"), category),
        .misses = stats_register_counter(stats, FL_S("misses"), category),
        .evictions = stats_register_counter(stats, FL_S("evictions"), category),
        .bytes_used = stats_register_counter(stats, FL_S("bytes_used"), category),
    };
}

ShapedRunCache* shaped_run_cache_new(FlArena* arena, ShapedRunCacheConfig config) {
    ShapedRunCache* self = arena_alloc_zero(arena, ShapedRunCache);
    self->arena = arena_new();
    self->unpacked = arena_new();
    self->shaper = font_shaper_new(self->arena);
    self->victims = arena_alloc_array(self->arena, u64, MAX_ENTRIES);
    self->pool_bytes = tlsf_size() + tlsf_pool_overhead() + config.pool_bytes;
    self->pool_memory = fl_arena_alloc_raw(self->arena, self->pool_bytes, _Alignof(max_align_t));
    self->pool = tlsf_create_with_pool(self->pool_memory, self->pool_bytes);
    self->stats = config.stats;
    self->ids = register_counters(config.stats);
    hashmap_new(&self->entries, self->arena, BUCKET_COUNT);
    return self;
}

void shaped_run_cache_destroy(ShapedRunCache* self) {
    font_shaper_destroy(self->shaper);
    fl_arena_destroy(self->unpacked);
    fl_arena_destroy(self->arena);
}

void shaped_run_cache_next_frame(ShapedRunCache* self) {
    self->frame++;
}

static void set_bytes_used(ShapedRunCache* self, u64 bytes) {
    self->bytes_used = bytes;
    stats_set(self->stats, self->ids.bytes_used, bytes);
}

void shaped_run_cache_clear(ShapedRunCache* self) {
    hashmap_clear(&self->entries);
    self->pool = tlsf_create_with_pool(self->pool_memory, self->pool_bytes);
    set_bytes_used(self, 0);
}

static RunKey run_key(const FontShaperRun* run, FlString text) {
    const KeyContext context = { .primary = run->primary.id, .fallback = run->fallback.id, .length = text.length };
    const u64 seed = XXH3_64bits(&context, sizeof(context));
    const XXH128_hash_t hash = XXH3_128bits_withSeed(text.data, text.length, seed);
    return (RunKey) { .low = hash.low64, .high = hash.high64 };
}

static ShapedRunEntry* find_entry(ShapedRunCache* self, const RunKey* key) {
    ShapedRunEntry* entry = hashmap_get(&self->entries, key->low);
    if (entry == nullptr || entry->hash_high != key->high) {
        return nullptr;
    }
    return entry;
}

static u64 offset_bytes(u32 count, u16 flags) {
    return (flags & Entry_Offsets) != 0 ? count * sizeof(PackedOffset) : 0;
}

static u64 face_bytes(u32 count, u16 flags) {
    return (flags & Entry_Faces) != 0 ? count : 0;
}

static u64 packed_bytes(u32 count, u16 flags) {
    return count * sizeof(PackedGlyph) + offset_bytes(count, flags) + face_bytes(count, flags);
}

#ifndef NDEBUG
static u64 text_copy_bytes(FlString text) {
    return sizeof(u32) + text.length;
}

static void text_copy_write(u8* dest, FlString text) {
    const u32 length = (u32)text.length;
    memcpy(dest, &length, sizeof(length));
    memcpy(dest + sizeof(length), text.data, text.length);
}

static void text_copy_check(const u8* copy, FlString text) {
    u32 length;
    memcpy(&length, copy, sizeof(length));
    FL_ASSERT(length == text.length);
    FL_ASSERT(memcmp(copy + sizeof(length), text.data, text.length) == 0);
}
#else
static u64 text_copy_bytes(FlString text) {
    (void)text;
    return 0;
}

static void text_copy_write(u8* dest, FlString text) {
    (void)dest;
    (void)text;
}

static void text_copy_check(const u8* copy, FlString text) {
    (void)copy;
    (void)text;
}
#endif

static u16 run_flags(const FontUnitRun* units) {
    const u16 offsets = units->has_offsets ? Entry_Offsets : 0;
    const u16 faces = units->has_faces ? Entry_Faces : 0;
    return offsets | faces;
}

static Sections block_sections(u8* block, u32 count, u16 flags) {
    u8* offsets = block + count * sizeof(PackedGlyph);
    u8* faces = offsets + offset_bytes(count, flags);
    return (Sections) {
        .glyphs = (PackedGlyph*)block,
        .offsets = (flags & Entry_Offsets) != 0 ? (PackedOffset*)offsets : nullptr,
        .faces = (flags & Entry_Faces) != 0 ? faces : nullptr,
    };
}

static void pack_run(u8* block, const FontUnitRun* units) {
    const Sections sections = block_sections(block, units->count, run_flags(units));
    for_count(i, units->count) {
        const FontUnitGlyph* glyph = &units->glyphs[i];
        sections.glyphs[i] = (PackedGlyph) { .glyph_index = glyph->glyph_index, .advance = glyph->advance };
        if (sections.offsets != nullptr) {
            sections.offsets[i] = (PackedOffset) { .x = glyph->offset_x, .y = glyph->offset_y };
        }
        if (sections.faces != nullptr) {
            sections.faces[i] = glyph->face;
        }
    }
}

static FontUnitGlyph unpack_glyph(const Sections* sections, u32 index) {
    const PackedOffset offset = sections->offsets != nullptr ? sections->offsets[index] : (PackedOffset) { 0 };
    return (FontUnitGlyph) {
        .glyph_index = sections->glyphs[index].glyph_index,
        .advance = sections->glyphs[index].advance,
        .offset_x = offset.x,
        .offset_y = offset.y,
        .face = sections->faces != nullptr ? sections->faces[index] : 0,
    };
}

static void release_block(ShapedRunCache* self, const ShapedRunHot* hot) {
    if (hot->block == NO_BLOCK) {
        return;
    }
    void* block = self->pool_memory + hot->block;
    set_bytes_used(self, self->bytes_used - tlsf_block_size(block) - tlsf_alloc_overhead());
    tlsf_free(self->pool, block);
}

static void sweep(ShapedRunCache* self, u32 idle_frames) {
    u32 count = 0;
    for_each_hashmap(&self->entries, key, entry) {
        if (self->frame - entry->hot.last_frame > idle_frames) {
            release_block(self, &entry->hot);
            self->victims[count++] = *key;
        }
    }
    for_count(i, count) {
        hashmap_remove(&self->entries, self->victims[i]);
    }
    stats_add(self->stats, self->ids.evictions, count);
}

static bool sweep_next(ShapedRunCache* self, u32* idle_frames) {
    if (*idle_frames == 0) {
        return false;
    }
    *idle_frames /= 2;
    sweep(self, *idle_frames);
    return true;
}

static void* pool_alloc(ShapedRunCache* self, u64 bytes) {
    void* block = tlsf_malloc(self->pool, bytes);
    u32 idle_frames = IDLE_FRAMES * 2;
    while (block == nullptr && sweep_next(self, &idle_frames)) {
        block = tlsf_malloc(self->pool, bytes);
    }
    if (block != nullptr) {
        set_bytes_used(self, self->bytes_used + tlsf_block_size(block) + tlsf_alloc_overhead());
    }
    return block;
}

static bool has_entry_room(ShapedRunCache* self) {
    u32 idle_frames = IDLE_FRAMES * 2;
    bool swept = true;
    while (hashmap_count(&self->entries) >= MAX_ENTRIES && swept) {
        swept = sweep_next(self, &idle_frames);
    }
    return hashmap_count(&self->entries) < MAX_ENTRIES;
}

static bool store_block(ShapedRunCache* self, const FontUnitRun* units, FlString text, ShapedRunHot* hot) {
    if (units->count == 0) {
        hot->flags = 0;
        return true;
    }
    if (units->count > UINT16_MAX) {
        return false;
    }
    const u64 packed = packed_bytes(units->count, run_flags(units));
    u8* block = pool_alloc(self, packed + text_copy_bytes(text));
    if (block == nullptr) {
        return false;
    }
    pack_run(block, units);
    text_copy_write(block + packed, text);
    hot->block = (u32)(block - self->pool_memory);
    hot->glyph_count = (u16)units->count;
    hot->width_units = units->width_units;
    hot->flags = run_flags(units);
    return true;
}

static void put_entry(ShapedRunCache* self, const RunKey* key, const ShapedRunHot* hot) {
    const ShapedRunEntry* replaced = hashmap_get(&self->entries, key->low);
    if (replaced != nullptr) {
        release_block(self, &replaced->hot);
        stats_inc(self->stats, self->ids.evictions);
    }
    const ShapedRunEntry entry = { .hot = *hot, .hash_high = key->high };
    hashmap_insert(&self->entries, key->low, entry);
}

static void remember(ShapedRunCache* self, const RunKey* key, const FontUnitRun* units, FlString text) {
    ShapedRunHot hot = { .last_frame = self->frame, .block = NO_BLOCK, .flags = Entry_Unshaped };
    if (!has_entry_room(self)) {
        return;
    }
    if (units != nullptr && !store_block(self, units, text, &hot)) {
        return;
    }
    put_entry(self, key, &hot);
}

static FontUnitRun unpack(ShapedRunCache* self, const ShapedRunHot* hot) {
    const Sections sections = block_sections(self->pool_memory + hot->block, hot->glyph_count, hot->flags);
    fl_arena_rewind(self->unpacked);
    FontUnitGlyph* glyphs = arena_alloc_array(self->unpacked, FontUnitGlyph, hot->glyph_count);
    for_count(i, hot->glyph_count) {
        glyphs[i] = unpack_glyph(&sections, i);
    }
    return (FontUnitRun) { .glyphs = glyphs };
}

static FontUnitRun entry_run(ShapedRunCache* self, const ShapedRunHot* hot, const FontShaperRun* run) {
    const bool has_faces = (hot->flags & Entry_Faces) != 0;
    const bool reads_glyphs = run->glyph_arena != nullptr || has_faces;
    FontUnitRun units = { 0 };
    if (hot->block != NO_BLOCK && reads_glyphs) {
        units = unpack(self, hot);
    }
    units.count = hot->glyph_count;
    units.width_units = hot->width_units;
    units.has_offsets = (hot->flags & Entry_Offsets) != 0;
    units.has_faces = has_faces;
    return units;
}

static bool serve_hit(ShapedRunCache* self, ShapedRunEntry* entry, const FontShaperRun* run, FlShapedRun* out) {
    stats_inc(self->stats, self->ids.hits);
    entry->hot.last_frame = self->frame;
    if ((entry->hot.flags & Entry_Unshaped) != 0) {
        return false;
    }
    const FontUnitRun units = entry_run(self, &entry->hot, run);
    font_shaper_place(&units, run, out);
    return true;
}

static void check_text(const ShapedRunCache* self, const ShapedRunHot* hot, FlString text) {
    if (hot->block == NO_BLOCK) {
        return;
    }
    const u8* block = self->pool_memory + hot->block;
    text_copy_check(block + packed_bytes(hot->glyph_count, hot->flags), text);
}

bool shaped_run_cache_get(ShapedRunCache* self, const FontShaperRun* run, FlString text, FlShapedRun* out) {
    const bool cacheable = text.length <= MAX_TEXT_BYTES;
    const RunKey key = cacheable ? run_key(run, text) : (RunKey) { 0 };
    ShapedRunEntry* entry = cacheable ? find_entry(self, &key) : nullptr;
    if (entry != nullptr) {
        check_text(self, &entry->hot, text);
        return serve_hit(self, entry, run, out);
    }

    FontUnitRun units;
    stats_inc(self->stats, self->ids.misses);
    const bool shaped = font_shaper_shape(self->shaper, run, text, &units);
    if (cacheable) {
        remember(self, &key, shaped ? &units : nullptr, text);
    }
    if (shaped) {
        font_shaper_place(&units, run, out);
    }
    return shaped;
}
