#include <core/arena.h>
#include <core/core.h>
#include <core/string.h>
#include <core/fixed_array.h>
#include <core/hashmap.h>
#include <core/jobsys.h>
#include <core/log.h>
#include <core/pool_allocator.h>
#include <core/sprintf.h>
#include <core/stats.h>
#include <core/string_allocator.h>
#include <core/tlsf.h>
#include <ui/image_private.h>
#include "sr_image_convert.h"
#include <core/memory.h>
#include <core/os/os.h>
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define IMAGE_CACHE_MEMORY_SIZE_MB 512
#define IMAGE_CACHE_EVICTED_BUCKETS 256

// Atomic: written by main thread (cache_new/cache_destroy), read by worker threads (alloc_entry, mark_finished)
static _Atomic(struct ImageCache*) s_image_cache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Statistics

typedef struct ImageCacheStats {
    u32 cache_misses;
    u32 images_cached;
    u32 evictions;
    u32 allocations;
    u32 allocation_failures;
    u32 eviction_cleanup_runs;
} ImageCacheStats;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void image_cache_register_stats(StatsRegistry* stats, ImageCacheStats* ids) {
    if (!stats) {
        return;
    }

    ids->cache_misses = stats_register_counter(stats, FL_S("cache_misses"), FL_S("image_cache"));
    ids->images_cached = stats_register_counter(stats, FL_S("images_cached"), FL_S("image_cache"));
    ids->evictions = stats_register_counter(stats, FL_S("evictions"), FL_S("image_cache"));
    ids->allocations = stats_register_counter(stats, FL_S("allocations"), FL_S("image_cache"));
    ids->allocation_failures = stats_register_counter(stats, FL_S("allocation_failures"), FL_S("image_cache"));
    ids->eviction_cleanup_runs = stats_register_counter(stats, FL_S("eviction_cleanup_runs"), FL_S("image_cache"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct InternalImage {
    ImageData public;
    ImageCacheEntry* cache_entry;
} InternalImage;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ImageCache {
    CACHE_ALIGNED _Atomic u64 current_frame;
    FlArena* arena;

    void* memory_pool;
    tlsf_t tlsf;

    StringAllocator* string_allocator;
    hashmap(ImageHandle, ImageCacheEntry*) handle_lookup;
    hashmap(ImageHandle, EvictedImageEntry*) evicted_lookup;

    pool(EvictedImageEntry) evicted_pool;

    Mutex mutex;
    ImageCacheConfig config;

    // Deferred-free list: blocks evicted or freed on a job thread (under the mutex) that the render thread may
    // still be reading. image_cache_update returns them to TLSF at the top of render(). Arena-backed, doubled
    // on growth; abandoned arrays stay in the per-cache arena.
    void** pending_free;
    u32 pending_free_count;
    u32 pending_free_cap;

    StatsRegistry* stats;
    ImageCacheStats stat_ids;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 image_format_bytes_per_pixel(InternalImageData* image) {
    // The channels field always matches the actual bytes per pixel, deinterleaved included
    return (u32)image->channels;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Batch size for eviction - collect this many oldest entries in a single scan
#define EVICTION_BATCH_SIZE 32

typedef struct EvictionBatch {
    ImageCacheEntry* entries[EVICTION_BATCH_SIZE];
    u64 frames[EVICTION_BATCH_SIZE];
    u32 count;
} EvictionBatch;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find the N oldest entries in a single hashmap scan

static void find_oldest_entries(ImageCache* self, EvictionBatch* batch) {
    batch->count = 0;

    for_each_hashmap(&self->handle_lookup, handle, entry) {
        (void)handle;

        // Images without a mount cannot be reloaded and are never evicted.
        if ((*entry)->mount == nullptr) {
            continue;
        }

        u64 last_frame = atomic_load(&(*entry)->last_access_frame);

        if (batch->count < EVICTION_BATCH_SIZE) {
            batch->entries[batch->count] = *entry;
            batch->frames[batch->count] = last_frame;
            batch->count++;
        } else {
            // Find the newest entry in the batch (the one we'd replace)
            u32 newest_idx = 0;
            u64 newest_frame = batch->frames[0];
            for (u32 i = 1; i < EVICTION_BATCH_SIZE; i++) {
                if (batch->frames[i] > newest_frame) {
                    newest_frame = batch->frames[i];
                    newest_idx = i;
                }
            }
            if (last_frame < newest_frame) {
                batch->entries[newest_idx] = *entry;
                batch->frames[newest_idx] = last_frame;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString image_cache_copy_string(FlString str) {
    ImageCache* self = atomic_load(&s_image_cache);

    // No pool exists until a software backend is created.
    if (!self) {
        return string_empty();
    }

    // Protect string allocator (arena) access with cache mutex
    mutex_lock(&self->mutex);
    FlString result = string_allocator_copy(self->string_allocator, str);
    mutex_unlock(&self->mutex);

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageCache* image_cache_new(FlArena* parent_arena, StatsRegistry* stats, ImageCacheConfig config) {
    // Process-singleton pool: the existing one is shared and never torn down.
    ImageCache* existing = atomic_load(&s_image_cache);
    if (existing) {
        return existing;
    }

    // A dedicated arena, so the 512 MB TLSF pool does not bloat the parent arena
    FlArena* arena = arena_new();
    ImageCache* self = arena_alloc_zero(arena, ImageCache);

    self->arena = arena;
    (void)parent_arena;
    self->string_allocator = string_allocator_new(arena);
    self->config = config;
    self->stats = stats;

    image_cache_register_stats(stats, &self->stat_ids);

    if (self->config.max_evicted_entries == 0) {
        self->config.max_evicted_entries = 10000;
        self->config.evicted_cleanup_frames = 36000;
        self->config.enable_evicted_cache = true;
    }

    mutex_init(&self->mutex);

    hashmap_new(&self->handle_lookup, arena, 64);
    hashmap_new(&self->evicted_lookup, arena, 256);

    pool_new(&self->evicted_pool, arena);

    self->memory_pool = arena_alloc_array(arena, u8, MB(IMAGE_CACHE_MEMORY_SIZE_MB));
    self->tlsf = tlsf_create_with_pool(self->memory_pool, MB(IMAGE_CACHE_MEMORY_SIZE_MB));
    if (!self->tlsf) {
        log_error("Failed to initialize TLSF allocator");
        return nullptr;
    }

    log_debug("Image cache initialized with %dMB TLSF pool", IMAGE_CACHE_MEMORY_SIZE_MB);

    // Publish if the slot is still empty. A thread that loses the race destroys its own unpublished pool
    // (no other thread has seen it) and returns the winner's; the slot never goes from a pool back to empty.
    ImageCache* winner = nullptr;
    if (atomic_compare_exchange_strong(&s_image_cache, &winner, self)) {
        return self;
    }

    tlsf_destroy(self->tlsf);
    mutex_destroy(&self->mutex);
    fl_arena_destroy(self->arena);

    return winner;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The pool is a process singleton that outlives every backend; nothing is freed here and process exit
// reclaims it.
void image_cache_destroy(ImageCache* self) {
    FL_VALIDATE(self != nullptr);
    (void)self;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_cache_update(ImageCache* self, u64 frame_counter) {
    atomic_store(&self->current_frame, frame_counter);

    // Drain the deferred-free list. Runs on the render thread before this frame's image_cache_get_data calls,
    // so blocks queued by earlier frames are no longer being read.
    mutex_lock(&self->mutex);
    for (u32 i = 0; i < self->pending_free_count; i++) {
        tlsf_free(self->tlsf, self->pending_free[i]);
    }
    self->pending_free_count = 0;
    mutex_unlock(&self->mutex);

    // Very infrequent cleanup - every 10 seconds at 60fps
    if (frame_counter % 600 == 0) {
        if (self->config.enable_evicted_cache) {
            mutex_lock(&self->mutex);

            u64 current = atomic_load(&self->current_frame);
            u64 cleanup_threshold = self->config.evicted_cleanup_frames;
            int removed_count = 0;

            arena_scratch_auto(temp);

            u32 evicted_count = hashmap_count(&self->evicted_lookup);
            if (evicted_count > 0) {
                fixed_array(ImageHandle) to_remove;
                fixed_array_new(&to_remove, temp.arena, evicted_count);

                // Collect keys to remove (to avoid modifying hashmap during iteration)
                for_each_hashmap(&self->evicted_lookup, handle, evicted_ptr) {
                    EvictedImageEntry* evicted = *evicted_ptr;
                    u64 age = current - evicted->eviction_frame;
                    if (age > cleanup_threshold) {
                        fixed_array_add(&to_remove, *handle);
                    }
                }

                for_each_fixed_array(handle, &to_remove) {
                    EvictedImageEntry** evicted_ptr = hashmap_get(&self->evicted_lookup, handle);
                    if (evicted_ptr) {
                        EvictedImageEntry* evicted = *evicted_ptr;
                        // FlString is managed by string_allocator, no need to free explicitly
                        pool_free(&self->evicted_pool, evicted);
                        hashmap_remove(&self->evicted_lookup, handle);
                        removed_count++;
                    }
                }

                if (removed_count > 0) {
                    log_debug("Evicted cache cleanup: Removed %d stale entries (threshold: %llu frames)", removed_count,
                              cleanup_threshold);
                }
            }

            stats_inc(self->stats, self->stat_ids.eviction_cleanup_runs);
            mutex_unlock(&self->mutex);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void image_cache_track_eviction(ImageCache* self, ImageCacheEntry* entry, ImageHandle handle) {
    if (!self->config.enable_evicted_cache)
        return;

    if (hashmap_count(&self->evicted_lookup) >= self->config.max_evicted_entries) {
        return;
    }

    // Synthetic images (no mount) can't be reloaded, so they never enter the evicted cache
    if (entry->mount == nullptr) {
        return;
    }

    // NOTE: This function is called within mutex-protected sections
    EvictedImageEntry* evicted = pool_alloc(&self->evicted_pool);
    evicted->mount = entry->mount;
    evicted->relative_path = entry->relative_path; // Just copy the string (already allocated on string allocator)
    evicted->options = entry->options;
    evicted->eviction_frame = atomic_load(&self->current_frame);

    hashmap_insert(&self->evicted_lookup, handle, evicted);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Queue a TLSF block for release at the next frame-boundary drain (image_cache_update). Called under the cache
// mutex from the job-thread eviction/free paths so we never tlsf_free pixels the render thread may still be
// reading for the in-flight frame. Growing the list abandons the old array in the arena (pointer-sized, per the
// struct comment).
static void image_cache_defer_free(ImageCache* self, void* data) {
    if (!data) {
        return;
    }

    if (self->pending_free_count == self->pending_free_cap) {
        u32 new_cap = self->pending_free_cap ? self->pending_free_cap * 2 : 16;
        void** grown = arena_alloc_array(self->arena, void*, new_cap);
        if (self->pending_free_count > 0) {
            memcpy(grown, self->pending_free, self->pending_free_count * sizeof(void*));
        }
        self->pending_free = grown;
        self->pending_free_cap = new_cap;
    }

    self->pending_free[self->pending_free_count++] = data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void evict_single_entry(ImageCache* self, ImageCacheEntry* entry) {
    ImageHandle handle = entry->handle;
    if (handle != IMAGE_HANDLE_INVALID) {
        image_cache_track_eviction(self, entry, handle);
        hashmap_remove(&self->handle_lookup, handle);
    }

    image_cache_defer_free(self, entry->public_data.data);
    stats_inc(self->stats, self->stat_ids.evictions);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Evict a batch of oldest entries in one go (single scan + batch free)

static u32 evict_batch(ImageCache* self) {
    EvictionBatch batch = { 0 };
    find_oldest_entries(self, &batch);

    for (u32 i = 0; i < batch.count; i++) {
        evict_single_entry(self, batch.entries[i]);
    }

    return batch.count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageCacheEntry* image_cache_alloc_entry(InternalImageData* image) {
    ImageCache* self = atomic_load(&s_image_cache);

    FL_VALIDATE_RET(self != nullptr, nullptr);

    u32 bytes_per_pixel = image_format_bytes_per_pixel(image);
    size_t required_size = (size_t)image->padded_width * image->padded_height * bytes_per_pixel;
    size_t alloc_size = required_size + SR_SCALE_PLANE_LOAD_GUARD;

    mutex_lock(&self->mutex);

    void* data = tlsf_malloc(self->tlsf, alloc_size);

    while (!data) {
        u32 evicted = evict_batch(self);
        data = tlsf_malloc(self->tlsf, alloc_size);
        if (!data && (evicted == 0 || hashmap_count(&self->handle_lookup) == 0)) {
            log_error("Failed to allocate %zu bytes even after evicting all entries", alloc_size);
            stats_inc(self->stats, self->stat_ids.allocation_failures);
            mutex_unlock(&self->mutex);
            return nullptr;
        }
    }

    ImageCacheEntry* entry = arena_alloc_zero(self->arena, ImageCacheEntry);
    entry->public_data.data = data;
    entry->public_data.width = image->width;
    entry->public_data.height = image->height;
    entry->public_data.padded_width = image->padded_width;
    entry->public_data.padded_height = image->padded_height;
    entry->public_data.stride = image->stride;
    entry->public_data.channels = image->channels;
    entry->public_data.format = image->format;
    entry->allocated_size = tlsf_block_size(data);

    stats_inc(self->stats, self->stat_ids.allocations);

    atomic_store(&entry->last_access_frame, atomic_load(&self->current_frame));

    // The entry is not yet in handle_lookup, so the mutex is released before the large copy.
    mutex_unlock(&self->mutex);

    memset((u8*)data + required_size, 0, SR_SCALE_PLANE_LOAD_GUARD);
    if (image->data) {
        memcpy(data, image->data, required_size);
    }

    return entry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the data back given a ImageHandle. Will only show return non null after we marked the request as finished

ImageData* image_cache_get_data(ImageHandle handle) {
    ImageCache* self = atomic_load(&s_image_cache);

    FL_VALIDATE_RET(self != nullptr, nullptr);

    mutex_lock_auto(&self->mutex);

    ImageCacheEntry** entry_lookup = hashmap_get(&self->handle_lookup, handle);
    if (entry_lookup) {
        ImageCacheEntry* entry = *entry_lookup;
        atomic_store(&entry->last_access_frame, self->current_frame);
        return &entry->public_data;
    }

    EvictedImageEntry** evicted_lookup = hashmap_get(&self->evicted_lookup, handle);
    if (evicted_lookup) {
        EvictedImageEntry* evicted = *evicted_lookup;

        // Trigger async reload reusing the SAME handle
        ImageHandle reloaded_handle
            = image_reload_evicted(evicted->mount, evicted->relative_path, evicted->options, handle);

        if (reloaded_handle == handle) {
            // Remove from the evicted cache since a reload is in progress
            hashmap_remove(&self->evicted_lookup, handle);
        }

        // Return null - data will be available when reload completes
        return nullptr;
    }

    stats_inc(self->stats, self->stat_ids.cache_misses);
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Record a cache miss for an initial image load. Data is stored in the cache before image_get_data() is
// called, so the miss has to be counted when the load is initiated.

void image_cache_record_initial_miss(void) {
    ImageCache* self = atomic_load(&s_image_cache);
    if (self) {
        stats_inc(self->stats, self->stat_ids.cache_misses);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Called once we know the vfs has finished processing the image

void image_cache_mark_request_finished(ImageHandle handle, ImageCacheEntry* cache_entry) {
    ImageCache* self = atomic_load(&s_image_cache);

    if (!self) {
        log_error("image_cache_mark_request_finished: cache not initialized (handle=%u)", (unsigned)handle);
        return;
    }

    mutex_lock(&self->mutex);
    cache_entry->handle = handle;
    hashmap_insert(&self->handle_lookup, handle, cache_entry);
    stats_inc(self->stats, self->stat_ids.images_cached);
    mutex_unlock(&self->mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_cache_free_handle(ImageHandle handle) {
    ImageCache* self = atomic_load(&s_image_cache);
    if (!self)
        return;

    mutex_lock(&self->mutex);

    ImageCacheEntry** entry_lookup = hashmap_get(&self->handle_lookup, handle);
    if (entry_lookup) {
        ImageCacheEntry* entry = *entry_lookup;

        image_cache_track_eviction(self, entry, handle);

        hashmap_remove(&self->handle_lookup, handle);
        image_cache_defer_free(self, entry->public_data.data);

        mutex_unlock(&self->mutex);

        // No fl_vfs_close() here: image_get_data() already closed the VFS handle after the load, and the
        // evicted cache still needs the handle to trigger a reload.
        return;
    }

    EvictedImageEntry** evicted_ptr = hashmap_get(&self->evicted_lookup, handle);
    if (evicted_ptr) {
        // Already evicted - this is a double-free, just ignore it
        // The evicted cache keeps the handle alive for potential reload
        mutex_unlock(&self->mutex);
        return;
    }

    mutex_unlock(&self->mutex);
    // Not in either cache: invalid, already freed, or being reloaded after eviction. No fl_vfs_close() in any
    // of those cases - on a reloading handle it would cancel the reload.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pool-residency hooks for tests (see image_cache.h): evict_keep removes an entry from the lookup but keeps its
// pixels, and restore makes it resident again.

void* image_cache_test_evict_keep(ImageHandle handle) {
    ImageCache* self = atomic_load(&s_image_cache);
    if (!self) {
        return nullptr;
    }

    mutex_lock(&self->mutex);
    ImageCacheEntry** lookup = hashmap_get(&self->handle_lookup, handle);
    ImageCacheEntry* entry = lookup ? *lookup : nullptr;
    if (entry) {
        hashmap_remove(&self->handle_lookup, handle);
        stats_inc(self->stats, self->stat_ids.evictions);
    }
    mutex_unlock(&self->mutex);
    return entry;
}

bool image_cache_test_restore(ImageHandle handle, void* entry) {
    ImageCache* self = atomic_load(&s_image_cache);
    if (!self || !entry) {
        return false;
    }

    mutex_lock(&self->mutex);
    hashmap_insert(&self->handle_lookup, handle, (ImageCacheEntry*)entry);
    mutex_unlock(&self->mutex);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Test accessors.

ImageCache* image_cache_get_sys(void) {
    return atomic_load(&s_image_cache);
}

// Number of blocks queued for the deferred-free drain in image_cache_update.
u32 image_cache_test_pending_free_count(void) {
    ImageCache* self = atomic_load(&s_image_cache);
    if (!self) {
        return 0;
    }

    mutex_lock(&self->mutex);
    u32 count = self->pending_free_count;
    mutex_unlock(&self->mutex);
    return count;
}
