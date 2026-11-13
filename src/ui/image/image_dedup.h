#pragma once

// Dedup cache: maps a load's identity key (mount + path + options) to the request id that already serves it, so
// callers that ask for the same image every frame share one load.
//
// Keys live in an arena owned by the cache. Removed keys leave dead bytes there until a rebuild reclaims them.
// Not thread-safe: the owner calls it from one thread.

#include <core/arena.h>
#include <core/hashmap.h>
#include <core/string.h>
#include <core/types.h>
#include <flowi/ui/image.h>
#include <ui/render_backend.h>
#include <vfs/vfs.h>
#include "image_decode.h"

typedef struct ImageDedupCache {
    FlArena* arena; // Owns the map's buckets, nodes and key copies; replaced when the cache is rebuilt
    hashmap(FlString, FlImageLoadRequestId) map;
    u64 live_key_bytes;
    u64 dead_key_bytes; // Key bytes of removed entries, still held by arena until the next rebuild
} ImageDedupCache;

void image_dedup_init(ImageDedupCache* cache);
void image_dedup_destroy(ImageDedupCache* cache);

// The identity key for a load: mount instance, path and every option that changes the decoded pixels. Allocated
// from scratch.
FlString image_dedup_make_key(FlArena* scratch, const FlVfsMount* mount, FlString relative_path,
                              const ImageDecodeOptions* options, const ImageThumbnailOptions* thumb);

// The request id stored under key, or nullptr.
const FlImageLoadRequestId* image_dedup_get(ImageDedupCache* cache, FlString key);

// Stores request_id under key (copying the key).
void image_dedup_insert(ImageDedupCache* cache, FlString key, FlImageLoadRequestId request_id);

// Drops every entry that resolves to request_id, rebuilding the cache once the dead keys outweigh the live ones.
void image_dedup_forget(ImageDedupCache* cache, FlImageLoadRequestId request_id);

// Empties the cache when it holds more than max_entries; returns true when it did.
bool image_dedup_flush_over(ImageDedupCache* cache, u64 max_entries);

// Bytes of key storage held, live entries plus removed ones not yet reclaimed.
u64 image_dedup_key_bytes(const ImageDedupCache* cache);
