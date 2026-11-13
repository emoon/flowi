#include "image_system.h"
#include <core/arena.h>
#include <core/string.h>
#include <core/hashmap.h>
#include <core/jobsys.h>
#include <core/log.h>
#include <core/memory.h>
#include <core/os/os.h>
#include <ui/render_backend.h>
#include "image_dedup.h"
#include "image_raw_pixels.h"
#include "image_registry.h"
#include <vfs/vfs.h>
#include <stdatomic.h>

// In-memory load job handles are tracked (in a list that grows with the burst) so they can be recycled via
// fl_jobs_is_finished() and waited on at teardown. Without this, fire-and-forget jobs leak JobInfo slots.

#define IMAGE_PENDING_JOBS_INITIAL_CAPACITY 64

// Cap the dedup cache to bound arena growth: past the limit the cache is flushed. With ~80 bytes per key this allows
// ~800 KB before a flush.
#define DEDUP_CACHE_MAX_ENTRIES 10000

// A decode whose prepare_image call is deferred to the begin-frame pump (prepare_image_on_job unset).
// The entry's arena owns everything the deferred call needs - the entry itself, the decoded pixels, and the
// copied request path - and is released right after the main-thread call completes. Job worker threads
// enqueue; the main/render thread drains. Guarded by ImageSystem.deferred_lock.

// One prepare_image call: the decode result and the request it answers.
typedef struct PrepareCall {
    FlImageLoadRequestId request_id;
    const FlImageDecodeResult* decoded; // null when decode failed (the backend still sees the failure)
    FlImageLoadRequest request;
} PrepareCall;

typedef struct DeferredPrepare {
    struct DeferredPrepare* next;
    FlArena* arena;
    PrepareCall call;
} DeferredPrepare;

struct ImageSystem {
    // The render backend's image-preparation seam, copied off the FlRenderBackend at create time. Invoked on
    // decode-complete in the job worker thread when prepare_on_job is set, else from the begin-frame pump on
    // the main/render thread.
    void* backend_user_data;
    bool (*prepare_image)(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                          FlArena* scratch, const FlImageLoadRequest* request);
    void (*release_image)(void* user_data, FlImageHandle handle);
    bool prepare_on_job;

    // Deferred prepare_image queue (FIFO so images realize in decode order).
    Mutex deferred_lock;
    DeferredPrepare* deferred_head;
    DeferredPrepare* deferred_tail;

    _Atomic float ui_scale;

    // Memory-load job handles, main thread only.
    FlJobHandle* pending_jobs;
    u32 pending_job_count;
    u32 pending_job_capacity;

    // Producers (memory-load jobs and VFS read callbacks) that have been submitted and not yet finished
    // touching this system. Raised on the submitting thread, lowered as the producer's last access.
    // Once closing is set no new producer is accepted and a job that has not reached the backend yet skips it.
    _Atomic u32 producers_inflight;
    _Atomic bool closing;
    ImageDedupCache dedup;

    // Failed in-memory decodes, keyed by synthetic request id. A memory load has no VFS handle behind it, so
    // a decode failure recorded here is the only way the poll path can distinguish "failed" from "still
    // pending". Written by memory_load_job on a job worker thread, read on the main thread, cleared on
    // image_free.
    Mutex failed_lock;
    hashmap(FlImageLoadRequestId, bool) failed_loads;
    hashmap(FlImageLoadRequestId, bool) active_synthetic_loads;

    // Main-thread only: both create_from_pixels and update_pixels realize synchronously.
    hashmap(FlImageLoadRequestId, RawPixelShape) raw_pixel_images;
};

static ImageDecodeOptions resolve_decode_options(ImageSystem* sys, const ImageDecodeOptions* options) {
    ImageDecodeOptions resolved = { 0 };
    if (options) {
        resolved = *options;
    }
    if (resolved.ui_scale <= 0.0f) {
        resolved.ui_scale = atomic_load(&sys->ui_scale);
    }
    return resolved;
}

typedef struct ImageLoadContext {
    ImageSystem* sys;
    _Atomic(FlImageLoadRequestId) request_id; // Atomic: written by main thread, read by worker thread
    FlVfsMount* mount;
    FlString relative_path;
    ImageDecodeOptions decode_options;
    ImageThumbnailOptions thumbnail_options;
    void* user_data;
} ImageLoadContext;

// Call the backend's prepare_image seam and mark the handle ready once the backend reports it realized. Runs on
// whichever thread the backend prepares on (prepare_on_job).

static void prepare_and_mark(ImageSystem* sys, const PrepareCall* call, FlArena* scratch) {
    if (sys->prepare_image(sys->backend_user_data, call->request_id, call->decoded, scratch, &call->request)) {
        image_registry_mark_realized((ImageHandle)call->request_id, call->decoded->width, call->decoded->height);
    }
}

// Hand a completed decode to the backend's prepare_image seam, from the job worker thread.
//
// With prepare_on_job the call happens here against the job's scratch arena; otherwise decode_arena owns the
// queued entry, the decoded result and a copy of the request path (the job context is gone by the time the
// begin-frame pump makes the call), and is destroyed once that call returns.

static void dispatch_prepare_image(ImageSystem* sys, FlArena* decode_arena, FlArena* scratch, const PrepareCall* call) {
    if (sys->prepare_on_job) {
        if (!atomic_load(&sys->closing)) {
            prepare_and_mark(sys, call, scratch);
        }
        return;
    }

    DeferredPrepare* entry = arena_alloc_zero(decode_arena, DeferredPrepare);
    entry->arena = decode_arena;
    entry->call = *call;
    entry->call.request.relative_path = fl_string_copy(decode_arena, call->request.relative_path);

    mutex_lock_auto(&sys->deferred_lock);
    if (sys->deferred_tail) {
        sys->deferred_tail->next = entry;
    } else {
        sys->deferred_head = entry;
    }
    sys->deferred_tail = entry;
}

// Decode data/size with the options in call->request and hand the result to the backend's prepare_image seam.
// Deferring backends decode into a dedicated arena the queued entry owns (released after the main-thread call);
// job-context backends decode into scratch. Sets call->decoded and returns false when the decode failed.

static bool decode_and_dispatch(ImageSystem* sys, const void* data, size_t size, PrepareCall* call) {
    arena_scratch_auto(scratch);

    bool defer = sys->prepare_image && !sys->prepare_on_job;
    FlArena* decode_arena = defer ? arena_new() : scratch.arena;

    FlImageDecodeResult* decoded = nullptr;
    if (data && size > 0) {
        decoded = image_decode(decode_arena, data, size, &call->request.decode_options);
        // Thumbnail/preview finishing pass (no-op for SVG/waveform decodes), on the same job arena.
        image_thumbnail_compose(decode_arena, decoded, &call->request.decode_options,
                                &call->request.thumbnail_options);
    }
    call->decoded = decoded;

    if (sys->prepare_image) {
        dispatch_prepare_image(sys, decode_arena, scratch.arena, call);
    }

    return decoded != nullptr;
}

// VFS callback - runs in job worker thread

static void log_decode_failure(FlString path, FlImageLoadRequestId request_id, const void* data, i64 size) {
    const char* what = (data && size > 0) ? "decode failed" : "no data";
    log_error("image_system_vfs_callback: %s for '%.*s' (data=%p, size=%lld)", what, (int)path.length, path.data, data,
              (long long)size);
    log_error("image_system_vfs_callback: pipeline failed for '%.*s' (request_id=%u)", (int)path.length, path.data,
              (unsigned)request_id);
}

static FlVfsData image_system_vfs_callback(void* data, i64 size, void* user_data) {
    ImageLoadContext* ctx = (ImageLoadContext*)user_data;
    ImageSystem* sys = ctx->sys;

    PrepareCall call = {
        .request_id = atomic_load(&ctx->request_id),
        .request = {
            .mount = ctx->mount,
            .relative_path = ctx->relative_path,
            .decode_options = ctx->decode_options,
            .thumbnail_options = ctx->thumbnail_options,
            .user_data = ctx->user_data,
        },
    };

    const bool decoded = decode_and_dispatch(sys, data, size > 0 ? (size_t)size : 0, &call);

    if (!decoded) {
        log_decode_failure(ctx->relative_path, call.request_id, data, size);
    }

    FlVfsData result = { 0 };
    result.success = decoded;

    // Nothing reads ctx past here: a deferred prepare_image call has its own arena copy of the path, and the
    // VFS never touches handle->user_data again after the callback returns. A failed or cancelled read still
    // arrives here with data == nullptr, so this is the single release point for both.
    fl_string_free(ctx->relative_path);
    jobs_context_free(ctx);

    // Last access to the image system from this producer.
    atomic_fetch_sub(&sys->producers_inflight, 1);

    return result;
}

// Register a producer before it is submitted. Refused once teardown has begun; the closing check follows the
// increment so a concurrent image_system_close either sees this producer or this call sees closing.
static bool producer_begin(ImageSystem* sys) {
    atomic_fetch_add(&sys->producers_inflight, 1);
    if (atomic_load(&sys->closing)) {
        atomic_fetch_sub(&sys->producers_inflight, 1);
        return false;
    }
    return true;
}

static void producer_end(ImageSystem* sys) {
    atomic_fetch_sub(&sys->producers_inflight, 1);
}

ImageSystem* image_system_create(FlArena* arena, const FlRenderBackend* backend) {
    ImageSystem* sys = arena_alloc_zero(arena, ImageSystem);
    image_dedup_init(&sys->dedup);
    if (backend) {
        sys->backend_user_data = backend->user_data;
        sys->prepare_image = backend->prepare_image;
        sys->release_image = backend->release_image;
        sys->prepare_on_job = backend->prepare_image_on_job;
    }
    mutex_init(&sys->deferred_lock);
    mutex_init(&sys->failed_lock);
    atomic_store(&sys->ui_scale, 1.0f);
    hashmap_new(&sys->failed_loads, arena, 16);
    hashmap_new(&sys->active_synthetic_loads, arena, 16);
    hashmap_new(&sys->raw_pixel_images, arena, 16);
    return sys;
}

// Detach the deferred queue under the lock; entries are consumed outside it so job threads never wait on a
// prepare_image call.

static DeferredPrepare* detach_deferred_prepares(ImageSystem* sys) {
    mutex_lock_auto(&sys->deferred_lock);
    DeferredPrepare* head = sys->deferred_head;
    sys->deferred_head = nullptr;
    sys->deferred_tail = nullptr;
    return head;
}

// Ends a memory load's PENDING state once no further acknowledgement can arrive: drops the active record and,
// when the backend did not realize the image, records the load as failed. A no-op for any other request id.

static void settle_synthetic_load(ImageSystem* sys, FlImageLoadRequestId request_id) {
    mutex_lock_auto(&sys->failed_lock);
    if (hashmap_get(&sys->active_synthetic_loads, request_id) == nullptr) {
        return;
    }
    hashmap_remove(&sys->active_synthetic_loads, request_id);

    if (!image_registry_is_ready((ImageHandle)request_id, nullptr, nullptr)) {
        bool failed = true;
        hashmap_insert(&sys->failed_loads, request_id, failed);
    }
}

static void wait_pending_jobs(ImageSystem* sys);

void image_system_close(ImageSystem* sys) {
    atomic_store(&sys->closing, true);

    // Producers only enqueue or call the backend and never wait on the render thread, so they always finish.
    while (atomic_load(&sys->producers_inflight) != 0) {
        sleep_us(100);
    }
    wait_pending_jobs(sys);

    // Nothing can enqueue any more. Release decodes still awaiting their deferred prepare_image call without
    // making it: the backend may already be going away. The entry arena owns the node, so grab next before
    // destroying.
    DeferredPrepare* entry = detach_deferred_prepares(sys);
    while (entry) {
        DeferredPrepare* next = entry->next;
        fl_arena_destroy(entry->arena);
        entry = next;
    }
}

void image_system_destroy(ImageSystem* sys) {
    image_system_close(sys);
    mi_free(sys->pending_jobs);
    sys->pending_jobs = nullptr;
    mutex_destroy(&sys->deferred_lock);
    mutex_destroy(&sys->failed_lock);

    image_dedup_destroy(&sys->dedup);
}

// Null-guarded: the SW backend leaves release_image unbound and relies on wholesale regen.

void image_system_release_image(ImageSystem* sys, FlImageHandle handle) {
    if (sys && sys->release_image) {
        sys->release_image(sys->backend_user_data, handle);
    }
}

void image_system_begin_frame(ImageSystem* sys) {
    // Pump deferred prepare_image calls on this (main/render) thread, then release the arena that kept each
    // decoded result alive.
    DeferredPrepare* entry = detach_deferred_prepares(sys);
    while (entry) {
        DeferredPrepare* next = entry->next;
        {
            arena_scratch_auto(scratch);
            prepare_and_mark(sys, &entry->call, scratch.arena);
        }
        settle_synthetic_load(sys, entry->call.request_id);
        fl_arena_destroy(entry->arena);
        entry = next;
    }

    // The cache is persistent across frames, so callers that load the same image every frame get dedup hits.
    if (image_dedup_flush_over(&sys->dedup, DEDUP_CACHE_MAX_ENTRIES)) {
        log_info("Image dedup cache flushed: more than %d entries", DEDUP_CACHE_MAX_ENTRIES);
    }
}

FlImageLoadRequestId image_system_load(ImageSystem* sys, FlVfsMount* mount, FlString relative_path,
                                       const ImageDecodeOptions* options, const ImageThumbnailOptions* thumb_options,
                                       void* user_data, FlImageLoadRequestId reuse_handle) {
    if (!sys || !mount) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    if (!producer_begin(sys)) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    ImageLoadContext* ctx = jobs_context_alloc(ImageLoadContext);
    if (!ctx) {
        producer_end(sys);
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    ctx->sys = sys;
    ctx->mount = mount;
    // Copy the path - the worker-thread callback reads it after image_system_load returns, but the ui.h
    // contract lets the caller free/reuse its buffer immediately. fl_string_copy_malloc passes static strings
    // through untouched and heap-copies dynamic ones; the callback releases the copy with fl_string_free.
    ctx->relative_path = fl_string_copy_malloc(relative_path);
    ctx->user_data = user_data;
    ctx->decode_options = resolve_decode_options(sys, options);
    ctx->thumbnail_options = thumb_options ? *thumb_options : (ImageThumbnailOptions) { 0 };

    // Two-phase VFS read: prepare the handle, store request_id, then dispatch; the worker reads request_id
    // only after it is stored.
    FlVfsHandle vfs_handle = vfs_mount_read_all_prepare(mount, relative_path, image_system_vfs_callback, ctx, nullptr,
                                                        (FlVfsHandle)reuse_handle);

    if (vfs_handle == FL_VFS_HANDLE_INVALID) {
        // image_system_vfs_callback never runs, so release what it would have released.
        fl_string_free(ctx->relative_path);
        jobs_context_free(ctx);
        producer_end(sys);
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    FlImageLoadRequestId request_id = (FlImageLoadRequestId)vfs_handle;
    atomic_store(&ctx->request_id, request_id);

    vfs_dispatch(vfs_handle);

    return request_id;
}

FlImageLoadRequestId image_system_load_dedup(ImageSystem* sys, FlVfsMount* mount, FlString relative_path,
                                             const ImageDecodeOptions* options,
                                             const ImageThumbnailOptions* thumb_options, void* user_data,
                                             bool* was_cached) {
    bool unused_was_cached;
    if (!was_cached) {
        was_cached = &unused_was_cached;
    }
    *was_cached = false;

    if (!sys || !mount) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    ImageThumbnailOptions thumb = thumb_options ? *thumb_options : (ImageThumbnailOptions) { 0 };

    arena_scratch_auto(scratch);
    FlString cache_key = image_dedup_make_key(scratch.arena, mount, relative_path, options, &thumb);

    const FlImageLoadRequestId* cached = image_dedup_get(&sys->dedup, cache_key);
    if (cached != nullptr) {
        *was_cached = true;
        return *cached;
    }

    FlImageLoadRequestId request_id
        = image_system_load(sys, mount, relative_path, options, &thumb, user_data, FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    if (request_id != FL_IMAGE_LOAD_REQUEST_ID_INVALID) {
        image_dedup_insert(&sys->dedup, cache_key, request_id);
    }

    return request_id;
}

void image_system_forget_dedup(ImageSystem* sys, FlImageLoadRequestId request_id) {
    if (sys) {
        image_dedup_forget(&sys->dedup, request_id);
    }
}

u64 image_system_dedup_key_bytes(const ImageSystem* sys) {
    return image_dedup_key_bytes(&sys->dedup);
}

// Drain completed memory-load jobs so their JobInfo slots get recycled.

static void drain_completed_jobs(ImageSystem* sys) {
    u32 i = 0;
    while (i < sys->pending_job_count) {
        // Anything but NotFinished (finished, or the slot was recycled) means the job is no longer running.
        if (fl_jobs_is_finished(sys->pending_jobs[i]) != FlJobsResult_NotFinished) {
            sys->pending_jobs[i] = sys->pending_jobs[sys->pending_job_count - 1];
            sys->pending_job_count--;
        } else {
            i++;
        }
    }
}

// Make room for one more tracked handle, so the list can take it after the job is submitted.
static bool reserve_pending_job(ImageSystem* sys) {
    if (sys->pending_job_count < sys->pending_job_capacity) {
        return true;
    }
    u32 capacity = sys->pending_job_capacity ? sys->pending_job_capacity * 2 : IMAGE_PENDING_JOBS_INITIAL_CAPACITY;
    FlJobHandle* jobs = (FlJobHandle*)mi_realloc(sys->pending_jobs, capacity * sizeof(FlJobHandle));
    if (!jobs) {
        log_error("image_system: out of memory tracking %u pending image jobs", capacity);
        return false;
    }
    sys->pending_jobs = jobs;
    sys->pending_job_capacity = capacity;
    return true;
}

static void wait_pending_jobs(ImageSystem* sys) {
    for (u32 i = 0; i < sys->pending_job_count; i++) {
        fl_jobs_wait(sys->pending_jobs[i]);
    }
    drain_completed_jobs(sys);
}

// In-memory image loading (no VFS I/O). Synthetic handles start at 0x80000000; VFS handles start at 1.

static _Atomic u32 s_synthetic_handle_counter = 0x80000000;

static FlImageLoadRequestId alloc_synthetic_handle(void) {
    u32 handle = atomic_fetch_add(&s_synthetic_handle_counter, 1);
    if (handle == FL_IMAGE_HANDLE_INVALID) {
        handle = atomic_fetch_add(&s_synthetic_handle_counter, 1);
    }
    return (FlImageLoadRequestId)handle;
}

typedef struct MemoryLoadContext {
    ImageSystem* sys;
    _Atomic(FlImageLoadRequestId) request_id;
    ImageDecodeOptions decode_options;
    ImageThumbnailOptions thumbnail_options;
    void* data; // Heap-allocated copy of source data (freed after decode)
    size_t data_size;
} MemoryLoadContext;

static void memory_load_job(void* user_data, FlJobsWorkerInfo info) {
    (void)info;
    MemoryLoadContext* ctx = (MemoryLoadContext*)user_data;
    ImageSystem* sys = ctx->sys;

    PrepareCall call = {
        .request_id = atomic_load(&ctx->request_id),
        .request = {
            .relative_path = FL_S("(memory)"),
            .decode_options = ctx->decode_options,
            .thumbnail_options = ctx->thumbnail_options,
        },
    };

    const bool decoded = decode_and_dispatch(sys, ctx->data, ctx->data_size, &call);

    if (!decoded) {
        log_error("memory_load_job: decode failed (request_id=%u, size=%zu)", (unsigned)call.request_id,
                  ctx->data_size);
        // No VFS handle carries an error status for a memory load; the poll path reads this record.
        image_system_mark_load_failed(sys, call.request_id);
    }

    // A deferring backend acknowledges from the begin-frame pump, which settles the load; every other path has
    // already had its one chance to acknowledge.
    bool deferred = sys->prepare_image && !sys->prepare_on_job;
    if (!decoded || !deferred) {
        settle_synthetic_load(sys, call.request_id);
    }

    mi_free(ctx->data);
    ctx->data = nullptr;

    // Last access to the image system from this producer.
    atomic_fetch_sub(&sys->producers_inflight, 1);
}

// Builds the job context for a memory load, copying data to the heap (the job frees it after decode). Returns
// nullptr when an allocation fails.
static MemoryLoadContext* memory_load_context_create(ImageSystem* sys, const void* data, size_t size) {
    MemoryLoadContext* ctx = jobs_context_alloc(MemoryLoadContext);
    if (!ctx) {
        return nullptr;
    }

    void* data_copy = mi_malloc(size);
    if (!data_copy) {
        // No job is submitted, so nothing would ever recycle the context.
        jobs_context_free(ctx);
        return nullptr;
    }
    memory_copy(data_copy, size, data, size);

    ctx->sys = sys;
    ctx->data = data_copy;
    ctx->data_size = size;
    return ctx;
}

// Registers the synthetic handle as active and submits the job under one failed_lock hold, so the job cannot settle
// the load before the active record exists. Needs a slot reserved by reserve_pending_job.
static FlImageLoadRequestId submit_memory_load(ImageSystem* sys, MemoryLoadContext* ctx) {
    FlImageLoadRequestId request_id = alloc_synthetic_handle();
    atomic_store(&ctx->request_id, request_id);

    mutex_lock_auto(&sys->failed_lock);
    bool active = true;
    hashmap_insert(&sys->active_synthetic_loads, request_id, active);

    FlJobHandle job_handle = fl_jobs_add_job(memory_load_job, ctx);
    sys->pending_jobs[sys->pending_job_count++] = job_handle;
    return request_id;
}

FlImageLoadRequestId image_system_load_from_memory(ImageSystem* sys, const void* data, size_t size,
                                                   const ImageDecodeOptions* options,
                                                   const ImageThumbnailOptions* thumb_options) {
    if (!sys || !data || size == 0) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    if (!producer_begin(sys)) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    drain_completed_jobs(sys);
    MemoryLoadContext* ctx = reserve_pending_job(sys) ? memory_load_context_create(sys, data, size) : nullptr;
    if (!ctx) {
        producer_end(sys);
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }
    ctx->decode_options = resolve_decode_options(sys, options);
    ctx->thumbnail_options = thumb_options ? *thumb_options : (ImageThumbnailOptions) { 0 };

    return submit_memory_load(sys, ctx);
}

void image_system_mark_load_failed(ImageSystem* sys, FlImageLoadRequestId request_id) {
    if (!sys || request_id == FL_IMAGE_LOAD_REQUEST_ID_INVALID) {
        return;
    }

    mutex_lock_auto(&sys->failed_lock);
    bool failed = true;
    hashmap_insert(&sys->failed_loads, request_id, failed);
}

bool image_system_load_failed(ImageSystem* sys, FlImageLoadRequestId request_id) {
    if (!sys) {
        return false;
    }

    mutex_lock_auto(&sys->failed_lock);
    return hashmap_get(&sys->failed_loads, request_id) != nullptr;
}

bool image_system_load_is_active_synthetic(ImageSystem* sys, FlImageLoadRequestId request_id) {
    if (!sys) {
        return false;
    }

    mutex_lock_auto(&sys->failed_lock);
    return hashmap_get(&sys->active_synthetic_loads, request_id) != nullptr;
}

void image_system_clear_load_failed(ImageSystem* sys, FlImageLoadRequestId request_id) {
    if (!sys) {
        return;
    }

    mutex_lock_auto(&sys->failed_lock);
    hashmap_remove(&sys->failed_loads, request_id);
    hashmap_remove(&sys->active_synthetic_loads, request_id);
}

// Raw pixels are realized synchronously through the backend, so they need pixels and a backend that prepares images.
static bool can_realize_pixels(const ImageSystem* sys, const void* pixels) {
    return sys && sys->prepare_image && pixels;
}

// Convert, realize, and ack one raw-pixel image under request_id.
static bool realize_raw_pixels(ImageSystem* sys, FlImageLoadRequestId request_id, const void* pixels,
                               RawPixelShape shape) {
    arena_scratch_auto(scratch);

    FlImageDecodeResult decoded;
    if (!image_raw_pixels_to_decoded(scratch.arena, pixels, shape, &decoded)) {
        log_error("image_system: unsupported raw image shape/format (%dx%d, format=%d)", shape.width, shape.height,
                  (int)shape.format);
        return false;
    }

    PrepareCall call = {
        .request_id = request_id,
        .decoded = &decoded,
        .request = {
            .relative_path = FL_S("(pixels)"),
            .decode_options = { .ui_scale = atomic_load(&sys->ui_scale) },
            .thumbnail_options = { .background_mode = ImageDecodeBackgroundMode_Regular },
            .dedicated = shape.dedicated,
        },
    };
    prepare_and_mark(sys, &call, scratch.arena);

    return image_registry_is_ready((ImageHandle)request_id, nullptr, nullptr);
}

FlImageLoadRequestId image_system_create_from_pixels(ImageSystem* sys, const void* pixels, RawPixelShape shape) {
    if (!can_realize_pixels(sys, pixels)) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    FlImageLoadRequestId request_id = alloc_synthetic_handle();
    if (!realize_raw_pixels(sys, request_id, pixels, shape)) {
        return FL_IMAGE_LOAD_REQUEST_ID_INVALID;
    }

    hashmap_insert(&sys->raw_pixel_images, request_id, shape);
    return request_id;
}

// Re-upload into a live handle. The handle's identity, dimensions, and format are all preserved.
//
// The old representation is released and the registry entry cleared BEFORE the new pixels are realized, so the
// backend's realization ack is the only thing that can make the handle ready again; clearing afterwards would
// leave a failed re-upload reporting "ready" against a handle the backend can no longer resolve.

bool image_system_update_pixels(ImageSystem* sys, FlImageHandle handle, const void* pixels) {
    if (!can_realize_pixels(sys, pixels)) {
        return false;
    }

    const RawPixelShape* record = hashmap_get(&sys->raw_pixel_images, (FlImageLoadRequestId)handle);
    if (record == nullptr) {
        log_error("image_system_update_pixels: handle %u was not created from pixels", (unsigned)handle);
        return false;
    }
    RawPixelShape shape = *record;

    if (sys->release_image) {
        sys->release_image(sys->backend_user_data, handle);
    }
    image_registry_remove((ImageHandle)handle);

    if (!realize_raw_pixels(sys, (FlImageLoadRequestId)handle, pixels, shape)) {
        // The handle keeps its creation record, so a later update with the same shape can still bring it back.
        log_error("image_system_update_pixels: handle %u could not be re-realized and is no longer ready",
                  (unsigned)handle);
        return false;
    }
    return true;
}

// Mint a handle for an image the backend realizes itself (a Viewport3d color target). No pixels cross the
// image system: the caller hands the handle to the backend, which registers its own texture under it and acks
// the registry.

FlImageHandle image_system_alloc_backend_handle(void) {
    return (FlImageHandle)alloc_synthetic_handle();
}

bool image_system_prepare_pending(ImageSystem* sys, FlImageLoadRequestId request_id) {
    mutex_lock_auto(&sys->deferred_lock);
    for_each_list(entry, sys->deferred_head) {
        if (entry->call.request_id == request_id) {
            return true;
        }
    }
    return false;
}

void image_system_set_ui_scale(ImageSystem* sys, float scale) {
    if (sys) {
        atomic_store(&sys->ui_scale, scale);
    }
}

float image_system_get_ui_scale(ImageSystem* sys) {
    if (sys) {
        return atomic_load(&sys->ui_scale);
    }
    return 1.0f;
}

u32 image_system_pending_job_count(ImageSystem* sys) {
    FL_VALIDATE_RET(sys != nullptr, 0);
    return sys->pending_job_count;
}
