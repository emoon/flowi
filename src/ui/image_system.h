#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image System - Loading pipeline bound to a render backend
//
// Loading is always async - the VFS read and decode run in a job worker thread, never stalling the caller.
// Where the backend's prepare_image seam runs is the backend's choice (prepare_image_on_job): set (SW), it is
// invoked in the job worker on decode-complete; unset (GL/D3D, thread-confined uploads), the decoded result is
// queued and the seam is invoked from image_system_begin_frame on the main/render thread.

#include <core/arena.h>
#include <core/types.h>
#include "image_decode.h"
#include <flowi/ui/image.h>
#include <ui/render_backend.h>
#include <vfs/vfs.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ImageSystem ImageSystem;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load request info (passed to the backend's prepare_image seam)

typedef struct FlImageLoadRequest {
    FlVfsMount* mount;
    FlString relative_path;
    ImageDecodeOptions decode_options;
    ImageThumbnailOptions thumbnail_options;
    void* user_data;

    // Keep this image out of any shared texture atlas - it must get a whole texture of its own. Required for
    // anything a mesh samples, whose UVs address the whole texture rather than an atlas sub-rect. Set at
    // creation and never inferred from size; a backend without an atlas ignores it.
    bool dedicated;
} FlImageLoadRequest;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Create the image system, bound to the render backend that realizes decoded images. The decoded result is
// valid only during the prepare_image callback - the backend must copy anything it keeps.
struct FlRenderBackend;
ImageSystem* image_system_create(FlArena* arena, const struct FlRenderBackend* backend);

// Stop accepting new loads, then wait for every load already running (memory-load jobs, VFS read callbacks) to
// finish, then discard decodes still awaiting their deferred prepare_image call without making it. After this returns
// no worker can reach the backend or the shared registry. Idempotent; image_system_destroy runs it too.
void image_system_close(ImageSystem* sys);

void image_system_destroy(ImageSystem* sys);

// Release a freed image's backend realization (GPU atlas cell / slot texture) via the backend's release_image
// seam. Null-guarded: no-op when no backend bound release_image (SW regenerates wholesale).
void image_system_release_image(ImageSystem* sys, FlImageHandle handle);

// Per-frame pump, called on the main/render thread.
// Invokes prepare_image for decodes deferred by the backend (prepare_image_on_job unset), releasing each
// decoded result afterwards, and flushes the dedup cache when it grows past its bound.
void image_system_begin_frame(ImageSystem* sys);

// True while a completed decode is queued for the begin-frame pump but its deferred prepare_image call has
// not run yet. Lets pollers report such a load as pending rather than failed.
bool image_system_prepare_pending(ImageSystem* sys, FlImageLoadRequestId request_id);

// Start an image load; returns FL_IMAGE_LOAD_REQUEST_ID_INVALID on failure.
// Pass reuse_handle to reuse an existing VFS handle (for evicted cache reload).
FlImageLoadRequestId image_system_load(ImageSystem* sys, FlVfsMount* mount, FlString relative_path,
                                       const ImageDecodeOptions* options, const ImageThumbnailOptions* thumb_options,
                                       void* user_data, FlImageLoadRequestId reuse_handle);

// Dedup-aware load: returns the existing request ID if the same mount instance + path + options was already
// loaded, setting *was_cached (no new load started).
FlImageLoadRequestId image_system_load_dedup(ImageSystem* sys, FlVfsMount* mount, FlString relative_path,
                                             const ImageDecodeOptions* options,
                                             const ImageThumbnailOptions* thumb_options, void* user_data,
                                             bool* was_cached);

// Drop every dedup entry that resolves to request_id, so the next dedup load of that image starts a new request.
void image_system_forget_dedup(ImageSystem* sys, FlImageLoadRequestId request_id);

// Bytes of dedup key storage the cache holds, live entries plus removed ones not yet reclaimed.
u64 image_system_dedup_key_bytes(const ImageSystem* sys);

// Load image from encoded data already in memory (no VFS I/O). Allocates a synthetic handle (not a VFS
// handle) and runs the decode + backend pipeline as a job; the data is copied internally, so the caller can
// free it as soon as this call returns. Poll with image_get_data() using the returned ID.
// Returns FL_IMAGE_LOAD_REQUEST_ID_INVALID on failure.
FlImageLoadRequestId image_system_load_from_memory(ImageSystem* sys, const void* data, size_t size,
                                                   const ImageDecodeOptions* options,
                                                   const ImageThumbnailOptions* thumb_options);

// Record/query terminal load failure by request id. Recording makes terminal errors stable after the underlying
// VFS handle is closed; it is also the only error source for synthetic in-memory handles.
void image_system_mark_load_failed(ImageSystem* sys, FlImageLoadRequestId request_id);
bool image_system_load_failed(ImageSystem* sys, FlImageLoadRequestId request_id);

// True while a synthetic in-memory request id belongs to this image system. Unlike a VFS handle, an active
// synthetic request has no canonical VFS status while it is pending.
bool image_system_load_is_active_synthetic(ImageSystem* sys, FlImageLoadRequestId request_id);

// Forget a recorded load failure (image_free), keeping the failed set bounded.
void image_system_clear_load_failed(ImageSystem* sys, FlImageLoadRequestId request_id);

// Copy raw pixels into a backend-owned static image and realize it synchronously through the active backend;
// dedicated selects the never-atlased path (see FlImageLoadRequest.dedicated).
// Returns FL_IMAGE_LOAD_REQUEST_ID_INVALID when the format/shape is unsupported or the backend cannot realize it.
FlImageLoadRequestId image_system_create_from_pixels(ImageSystem* sys, const void* pixels, int width, int height,
                                                     FlImageFormat format, bool dedicated);

// Re-upload into an image previously created from raw pixels, keeping the handle's identity, dimensions, and
// format. Returns false when the handle was not created from pixels or the backend cannot realize it.
bool image_system_update_pixels(ImageSystem* sys, FlImageHandle handle, const void* pixels);

// Mint an image handle for a texture the backend realizes and registers itself (a Viewport3d color target).
// No pixels pass through the image system; the handle draws like any other image once the backend acks it.
FlImageHandle image_system_alloc_backend_handle(void);

// UI scale, used for resolution-aware loading.
void image_system_set_ui_scale(ImageSystem* sys, float scale);
float image_system_get_ui_scale(ImageSystem* sys);

// Memory-load job handles still tracked (submitted and not yet recycled).
u32 image_system_pending_job_count(ImageSystem* sys);
