#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core <-> software image-cache boundary.
//
// The image cache is the software render backend's private map from image handle to its own planar/padded/TLSF
// representation (render/software/sr_image_cache.c); the pool exists only while that backend does. Core calls
// through this boundary no-op when no pool exists (a GPU-only context allocates only the readiness registry in
// image_registry.h). The SW-private planar structs live in image_private.h.

#include <core/stats.h>
#include "image.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImageCache ImageCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImageCacheConfig {
    bool enable_evicted_cache;
    u32 max_evicted_entries;
    u32 evicted_cleanup_frames; // Clean up metadata older than N frames
} ImageCacheConfig;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Create the software image cache. stats can be nullptr to disable statistics tracking.
ImageCache* image_cache_new(struct FlArena* arena, StatsRegistry* stats, ImageCacheConfig config);

void image_cache_destroy(ImageCache* self);

/// Update frame counter (call each frame). Driven by the software backend's render path, not the core.
void image_cache_update(ImageCache* self, u64 frame_counter);

/// Get the data back given an ImageHandle. Returns non-null only after the request is finished.
ImageData* image_cache_get_data(ImageHandle handle);

void image_cache_record_initial_miss(void);

void image_cache_free_handle(ImageHandle handle);

/// Reload an evicted image (implemented in image.c, called by the cache on evicted access). Returns the reused
/// handle (old_handle on success).
ImageHandle image_reload_evicted(FlVfsMount* mount, FlString relative_path, FlImageLoadOptions options,
                                 ImageHandle old_handle);

// Test-only hooks driving the SW pool residency transition (the headless harness has no VFS mount, so it cannot
// drive the real async reload). evict_keep removes a handle's pixels from the active (resident) lookup and returns
// the opaque entry (pixels retained, not freed); restore re-inserts that same entry. Returns nullptr/false if the
// handle is not currently resident.
void* image_cache_test_evict_keep(ImageHandle handle);
bool image_cache_test_restore(ImageHandle handle, void* entry);

// Global image cache singleton, for tests.
ImageCache* image_cache_get_sys(void);

// Number of blocks queued for the frame-boundary deferred-free drain (image_cache_update).
u32 image_cache_test_pending_free_count(void);
