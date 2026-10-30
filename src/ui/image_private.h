#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Software-backend-private image representation: the planar/padded form a static image takes once it has been
// realized for the software renderer (separate R/G/B/A planes, one pixel of bilinear padding, stored in the TLSF
// cache), plus the cache entry and eviction-metadata structs. Included only by the software image backend
// (src/render/software/sr_image_*.c). Core code uses the neutral
// boundary in image_cache.h instead.

#include <core/core.h>
#include "image.h"
#include "image_cache.h"
#include <core/types.h>
#include <vfs/vfs.h>
#include <stdatomic.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct InternalImageData {
    u8* data;
    int width;         // Logical width (original dimensions)
    int height;        // Logical height (original dimensions)
    int padded_width;  // Actual buffer width (width + 1 for bilinear padding)
    int padded_height; // Actual buffer height (height + 1 for bilinear padding)
    int stride;        // Bytes per row (including padding)
    int channels;      // Number of channels (1, 3, or 4)
    FlImageFormat format;
    VfsOpStatus status;
} InternalImageData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImageCacheEntry {
    CACHE_ALIGNED _Atomic u64 last_access_frame;
    ImageData public_data;
    size_t allocated_size;
    FlVfsMount* mount;
    FlString relative_path;
    FlImageLoadOptions options;
    ImageHandle handle;
    VfsOpStatus status;
} ImageCacheEntry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct EvictedImageEntry {
    FlVfsMount* mount;
    FlString relative_path;     // Path (string allocator)
    FlImageLoadOptions options; // Original load options
    _Atomic u64 eviction_frame;
} EvictedImageEntry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SW-cache internal entry points - called from the SW image backend, in the job worker thread.

/// Allocate an entry in the image cache (copies the planar pixels into the TLSF pool)
ImageCacheEntry* image_cache_alloc_entry(InternalImageData* image);

/// Copy string using the image cache string allocator
FlString image_cache_copy_string(FlString str);

/// Called once the VFS has finished processing the image
void image_cache_mark_request_finished(ImageHandle handle, ImageCacheEntry* entry);
