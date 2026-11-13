#include "image_dedup.h"
#include <core/sprintf.h>
#include <string.h>

// Removed entries leave their key bytes in the cache arena; once that dead weight passes this and outweighs the live
// keys, the cache is rebuilt from the live entries.
#define DEDUP_DEAD_KEY_BYTES_MAX 16384

#define DEDUP_INITIAL_BUCKETS 256

void image_dedup_init(ImageDedupCache* cache) {
    *cache = (ImageDedupCache) { .arena = arena_new() };
    hashmap_string_new(&cache->map, cache->arena, DEDUP_INITIAL_BUCKETS);
}

void image_dedup_destroy(ImageDedupCache* cache) {
    fl_arena_destroy(cache->arena);
    cache->arena = nullptr;
}

// Hash load options for the key (FNV-1a over the individual fields)

static u64 hash_load_options(const ImageDecodeOptions* o, const ImageThumbnailOptions* t) {
    u64 h = 14695981039346656037ULL;

#define FNV_MIX(val)           \
    do {                       \
        h ^= (u64)(val);       \
        h *= 1099511628211ULL; \
    } while (0)

    FNV_MIX(o->target_width);
    FNV_MIX(o->target_height);
    FNV_MIX(t->detect_black_border);
    FNV_MIX(o->preserve_aspect_ratio);
    FNV_MIX(t->background_mode);
    FNV_MIX(o->resize_filter);
    FNV_MIX(t->apply_vertical_fade);
    FNV_MIX(t->fade_r);
    FNV_MIX(t->fade_g);
    FNV_MIX(t->fade_b);
    FNV_MIX(o->detect_grayscale);

    u32 fade_bits;
    memcpy(&fade_bits, &t->fade_start, sizeof(fade_bits));
    FNV_MIX(fade_bits);

    u32 svg_bits;
    memcpy(&svg_bits, &o->svg_scale_factor, sizeof(svg_bits));
    FNV_MIX(svg_bits);

    u32 scale_bits;
    memcpy(&scale_bits, &o->ui_scale, sizeof(scale_bits));
    FNV_MIX(scale_bits);

#undef FNV_MIX

    return h;
}

FlString image_dedup_make_key(FlArena* scratch, const FlVfsMount* mount, FlString relative_path,
                              const ImageDecodeOptions* options, const ImageThumbnailOptions* thumb) {
    const u64 opts_hash = hash_load_options(options, thumb);
    return fl_sprintf_arena(scratch, "%llu:%S:%lx", (unsigned long long)vfs_mount_get_id(mount),
                            relative_path, opts_hash);
}

const FlImageLoadRequestId* image_dedup_get(ImageDedupCache* cache, FlString key) {
    return hashmap_string_get(&cache->map, key);
}

void image_dedup_insert(ImageDedupCache* cache, FlString key, FlImageLoadRequestId request_id) {
    hashmap_string_insert(&cache->map, key, request_id);
    cache->live_key_bytes += key.length;
}

// Replaces the arena and map with fresh ones, carrying the live entries over when keep_entries is set.
static void rebuild(ImageDedupCache* cache, bool keep_entries) {
    FlArena* fresh = arena_new();
    __typeof__(cache->map) old = cache->map;
    FlArena* old_arena = cache->arena;

    hashmap_string_new(&cache->map, fresh, DEDUP_INITIAL_BUCKETS);
    cache->arena = fresh;
    cache->live_key_bytes = 0;
    cache->dead_key_bytes = 0;

    if (keep_entries) {
        for_each_hashmap(&old, key, value) {
            hashmap_string_insert(&cache->map, *key, *value);
            cache->live_key_bytes += key->length;
        }
    }

    fl_arena_destroy(old_arena);
}

void image_dedup_forget(ImageDedupCache* cache, FlImageLoadRequestId request_id) {
    arena_scratch_auto(scratch);
    FlString* stale = arena_alloc_array(scratch.arena, FlString, hashmap_count(&cache->map));
    u64 stale_count = 0;

    for_each_hashmap(&cache->map, key, value) {
        if (*value == request_id) {
            stale[stale_count++] = *key;
        }
    }

    for_count(i, stale_count) {
        cache->live_key_bytes -= stale[i].length;
        cache->dead_key_bytes += stale[i].length;
        hashmap_string_remove(&cache->map, stale[i]);
    }

    if (cache->dead_key_bytes > DEDUP_DEAD_KEY_BYTES_MAX && cache->dead_key_bytes > cache->live_key_bytes) {
        rebuild(cache, true);
    }
}

bool image_dedup_flush_over(ImageDedupCache* cache, u64 max_entries) {
    if (hashmap_count(&cache->map) <= max_entries) {
        return false;
    }
    rebuild(cache, false);
    return true;
}

u64 image_dedup_key_bytes(const ImageDedupCache* cache) {
    return cache->live_key_bytes + cache->dead_key_bytes;
}
