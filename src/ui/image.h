#pragma once

#include <core/string.h>
#include <core/types.h>
#include <vfs/vfs.h>
#include <ui/ui_types.h>
#include "image_system.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define IMAGE_ERROR_INVALID_FORMAT (0x0001 << VFS_USER_SHIFT)
#define IMAGE_ERROR_DECODE_FAILED (0x0002 << VFS_USER_SHIFT)
#define IMAGE_ERROR_UNSUPPORTED_SVG (0x0003 << VFS_USER_SHIFT)
#define IMAGE_ERROR_RESIZE_FAILED (0x0004 << VFS_USER_SHIFT)
#define IMAGE_ERROR_OUT_OF_MEMORY (0x0005 << VFS_USER_SHIFT)
#define IMAGE_ERROR_IMAGE_CACHE_ERROR (0x0006 << VFS_USER_SHIFT)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef FlVfsHandle ImageHandle;
#define IMAGE_HANDLE_INVALID FL_IMAGE_HANDLE_INVALID

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImageData {
    u8* data;
    int width;         // Logical width (original dimensions)
    int height;        // Logical height (original dimensions)
    int padded_width;  // Actual buffer width (width + 1 for bilinear padding)
    int padded_height; // Actual buffer height (height + 1 for bilinear padding)
    int stride;        // Bytes per row (including padding)
    int channels;      // 1, 3, or 4
    FlImageFormat format;
} ImageData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Metadata-only result of an image query: logical dimensions + load status, never pixels. width/height are
// valid only when status is success.
typedef struct ImageResult {
    int width;
    int height;
    VfsOpStatus status;
} ImageResult;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Load async from VFS mount with options. Use image_get_data to get data back (or error)
ImageHandle image_load_async_with_options(FlVfsMount* mount, FlString relative_path, FlImageLoadOptions options);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Load async from VFS mount with options. Use image_get_data to get data back (or error)

#define image_load_async(mount, path, ...) \
    image_load_async_with_options(mount, path, (FlImageLoadOptions) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Load image from encoded data already in memory (PNG, JPEG, SVG, waveform, etc.)
/// Returns a handle immediately; poll with image_get_data().
/// The data is copied internally; caller can free it after this call.

ImageHandle image_load_from_memory(const void* data, size_t size, FlImageLoadOptions options);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Create a backend-owned static image from raw pixels already in memory.
/// Supports FlImageFormat_RGB, FlImageFormat_RGBA, FlImageFormat_Alpha, and FlImageFormat_XRGB.
/// Pixels are copied before return; the handle is ready immediately on success.

ImageHandle image_create_from_pixels(const void* pixels, int width, int height, FlImageFormat format);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// As above, but the image always gets a texture of its own and never enters a shared image atlas - required
/// for anything a mesh samples, since mesh UVs address the whole texture.

ImageHandle image_create_dedicated(const void* pixels, int width, int height, FlImageFormat format);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Re-upload into an image previously created from raw pixels, keeping the handle, dimensions, and format.

bool image_update_pixels(ImageHandle handle, const void* pixels);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Get data from an async load.

ImageResult image_get_data(ImageHandle handle);

/// The part of image_get_data that runs once a handle was observed not ready: failure, in-flight and
/// realized-since states, in that order.
ImageResult image_resolve_unrealized(ImageHandle handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum ImageStatus {
    ImageStatus_Pending,
    ImageStatus_Ready,
    ImageStatus_Error,
} ImageStatus;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Get the status of an image (pending, ready, or error)

ImageStatus image_get_status(ImageResult image);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Semantic public status for a load handle. Invalid and failed handles are Error; terminal states are stable.

FlImageStatus image_public_status(ImageHandle handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_free(ImageHandle handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set the load priority for a pending image load, e.g. to promote a prefetch image once it becomes visible.
// Has no effect if the image has already finished loading.

typedef enum ImageLoadPriority {
    ImageLoadPriority_Low = 0,    // Background/prefetch
    ImageLoadPriority_Normal = 1, // Default priority
    ImageLoadPriority_High = 2,   // Visible content
} ImageLoadPriority;

void image_set_load_priority(ImageHandle handle, ImageLoadPriority priority);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create an image that wraps an existing 4-byte-per-pixel buffer (FlImageFormat_RGBA or FlImageFormat_XRGB).
// The buffer stays caller-owned and must remain valid while the ImageData is used; the arena allocates only
// the ImageData struct. Returns nullptr on error.

ImageData* image_create_dynamic(u8* buffer, int width, int height, int stride, FlImageFormat format,
                                struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a dynamic image with a pre-allocated interleaved RGBA buffer. Returns nullptr on error.

FlDynamicImage* dynamic_image_create(int width, int height, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create a single-channel alpha dynamic image (is_alpha_only=true) for monochrome rendering; nullptr on error.
// Use with painter_dynamic_image() supplying a color to colorize the alpha mask.

FlDynamicImage* dynamic_image_create_alpha(int width, int height, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create the image system (call once at startup, bound to the render backend that realizes images).
// Returns true if this call created the system (the caller owns it and should call image_destroy_system),
// false if it already existed (first-caller-wins; this call was a no-op).

struct FlRenderBackend;
bool image_create_system(struct FlArena* arena, const struct FlRenderBackend* backend);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy the image system (test cleanup, or shutdown)

void image_destroy_system(void);

// Stop accepting new image loads, then wait for loads already running to finish, so nothing a load worker can
// reach (render backend, shared image registry) is destroyed under it. Call before tearing those down;
// image_destroy_system does the same. No-op when no system exists.
void image_stop_loading(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start-of-frame pump: runs deferred backend uploads and flushes the dedup cache once it outgrows its bound

void image_begin_frame(void);
