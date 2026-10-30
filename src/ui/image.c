#include "image.h"
#include <core/arena.h>
#include <core/log.h>
#include "image_cache.h"
#include "image_decode.h"
#include "image_registry.h"
#include "image_system.h"
#include <ui/render_backend.h>
#include "ui.h"
#include "ui_private.h"
#include <vfs/vfs.h>
#include <ui/ui_theme.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ImageSystem* s_image_system = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void set_thumbnail_tint_color(ImageThumbnailOptions* opts, FlBackgroundMode mode) {
    if (mode == FlBackgroundMode_Tint) {
        const FlThemeColors* theme = ui_get_theme_colors(ui_get_ctx());
        opts->tint_r = theme->surface.base.r;
        opts->tint_g = theme->surface.base.g;
        opts->tint_b = theme->surface.base.b;
    } else if (mode == FlBackgroundMode_Center) {
        opts->tint_r = 0;
        opts->tint_g = 0;
        opts->tint_b = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool image_create_system(FlArena* arena, const FlRenderBackend* backend) {
    if (s_image_system) {
        return false;
    }
    s_image_system = image_system_create(arena, backend);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_stop_loading(void) {
    if (s_image_system) {
        image_system_close(s_image_system);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_destroy_system(void) {
    if (s_image_system) {
        image_system_destroy(s_image_system);
        s_image_system = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_begin_frame(void) {
    if (s_image_system) {
        image_system_begin_frame(s_image_system);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void convert_load_options(const FlImageLoadOptions* options, ImageDecodeOptions* out_decode,
                                 ImageThumbnailOptions* out_thumb) {
    ImageDecodeBackgroundMode bg_mode = ImageDecodeBackgroundMode_Regular;
    if (options->background_mode == FlBackgroundMode_Tint) {
        bg_mode = ImageDecodeBackgroundMode_Tint;
    } else if (options->background_mode == FlBackgroundMode_Center) {
        bg_mode = ImageDecodeBackgroundMode_Center;
    }

    *out_decode = (ImageDecodeOptions) {
        .target_width = options->target_width,
        .target_height = options->target_height,
        .svg_scale_factor = options->svg_scale_factor,
        .ui_scale = image_system_get_ui_scale(s_image_system),
        .resize_filter = (options->resize_filter == FlResizeFilter_Nearest) ? ImageDecodeResizeFilter_Nearest
                                                                            : ImageDecodeResizeFilter_Default,
        .preserve_aspect_ratio = options->preserve_aspect_ratio || options->svg_preserve_aspect_ratio,
        .detect_grayscale = true,
        .force_rgb = options->apply_vertical_fade, // Fade previews don't need alpha
    };

    *out_thumb = (ImageThumbnailOptions) {
        .background_mode = bg_mode,
        .detect_black_border = options->detect_black_border,
        .apply_vertical_fade = options->apply_vertical_fade,
        .fade_start = options->fade_start,
        .fade_r = options->fade_r,
        .fade_g = options->fade_g,
        .fade_b = options->fade_b,
    };

    set_thumbnail_tint_color(out_thumb, options->background_mode);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageHandle image_load_async_with_options(FlVfsMount* mount, FlString relative_path, FlImageLoadOptions options) {
    if (!s_image_system) {
        log_error("image_load_async_with_options: image system not initialized. Call image_create_system() first.");
        return IMAGE_HANDLE_INVALID;
    }

    ImageDecodeOptions decode_opts;
    ImageThumbnailOptions thumb_opts;
    convert_load_options(&options, &decode_opts, &thumb_opts);

    // Callers (e.g. custom list renderers) may load the same image every frame; the dedup cache returns the
    // existing request id for an identical mount instance + path + options.
    bool was_cached = false;
    FlImageLoadRequestId request_id = image_system_load_dedup(s_image_system, mount, relative_path, &decode_opts,
                                                              &thumb_opts, nullptr, &was_cached);

    if (!was_cached) {
        image_cache_record_initial_miss();
    }

    return (ImageHandle)request_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageHandle image_reload_evicted(FlVfsMount* mount, FlString relative_path, FlImageLoadOptions options,
                                 ImageHandle old_handle) {
    if (!s_image_system) {
        return IMAGE_HANDLE_INVALID;
    }

    ImageDecodeOptions decode_opts;
    ImageThumbnailOptions thumb_opts;
    convert_load_options(&options, &decode_opts, &thumb_opts);

    image_cache_record_initial_miss();

    FlImageLoadRequestId request_id = image_system_load(s_image_system, mount, relative_path, &decode_opts, &thumb_opts,
                                                        nullptr, (FlImageLoadRequestId)old_handle);
    return (ImageHandle)request_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageHandle image_load_from_memory(const void* data, size_t size, FlImageLoadOptions options) {
    if (!s_image_system) {
        log_error("image_load_from_memory: image system not initialized. Call image_create_system() first.");
        return IMAGE_HANDLE_INVALID;
    }

    ImageDecodeOptions decode_opts;
    ImageThumbnailOptions thumb_opts;
    convert_load_options(&options, &decode_opts, &thumb_opts);

    image_cache_record_initial_miss();

    FlImageLoadRequestId request_id
        = image_system_load_from_memory(s_image_system, data, size, &decode_opts, &thumb_opts);
    return (ImageHandle)request_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ImageHandle create_from_pixels(const void* pixels, int width, int height, FlImageFormat format, bool dedicated) {
    if (!s_image_system) {
        log_error("image_create_from_pixels: image system not initialized. Call image_create_system() first.");
        return IMAGE_HANDLE_INVALID;
    }

    FlImageLoadRequestId request_id
        = image_system_create_from_pixels(s_image_system, pixels, width, height, format, dedicated);
    if (request_id == FL_IMAGE_LOAD_REQUEST_ID_INVALID) {
        return IMAGE_HANDLE_INVALID;
    }

    return (ImageHandle)request_id;
}

ImageHandle image_create_from_pixels(const void* pixels, int width, int height, FlImageFormat format) {
    return create_from_pixels(pixels, width, height, format, false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageHandle image_create_dedicated(const void* pixels, int width, int height, FlImageFormat format) {
    return create_from_pixels(pixels, width, height, format, true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool image_update_pixels(ImageHandle handle, const void* pixels) {
    if (!s_image_system) {
        log_error("image_update_pixels: image system not initialized. Call image_create_system() first.");
        return false;
    }
    return image_system_update_pixels(s_image_system, (FlImageHandle)handle, pixels);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Success with the realized size when the registry reports the handle ready, else a pending placeholder.
static ImageResult image_ready_result(ImageHandle handle) {
    int width = 0;
    int height = 0;
    if (!image_registry_is_ready(handle, &width, &height)) {
        return (ImageResult) { .status = VFS_STATE_PENDING };
    }
    fl_vfs_close((FlVfsHandle)handle); // safe to call on an already-closed handle
    return (ImageResult) { .width = width, .height = height, .status = VFS_STATE_SUCCESS };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageResult image_get_data(ImageHandle handle) {
    if (handle == IMAGE_HANDLE_INVALID) {
        return (ImageResult) { .status = VFS_STATE_ERROR };
    }

    ImageResult ready = image_ready_result(handle);
    if (ready.status == VFS_STATE_SUCCESS) {
        return ready;
    }
    return image_resolve_unrealized(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status of a handle that was not ready when polled. The backend's realization ack lands before a memory load
// leaves the active set, so readiness is re-read once the load reads as inactive.

ImageResult image_resolve_unrealized(ImageHandle handle) {
    // A failed in-memory decode has no VFS handle behind it - the synthetic handle never resolves through the
    // VFS status checks below, so this is the only place a poller can observe the failure.
    if (image_system_load_failed(s_image_system, (FlImageLoadRequestId)handle)) {
        return (ImageResult) { .status = VFS_STATE_ERROR };
    }

    // Not realized yet - pending vs error still comes from the VFS/decode path. Handle is the VFS handle.
    FlVfsHandle vfs_handle = (FlVfsHandle)handle;

    VfsOpStatus vfs_status = vfs_get_status(vfs_handle);
    if (vfs_is_pending(vfs_status)) {
        return (ImageResult) { .status = VFS_STATE_PENDING };
    }

    if (image_system_load_is_active_synthetic(s_image_system, (FlImageLoadRequestId)handle)) {
        return (ImageResult) { .status = VFS_STATE_PENDING };
    }

    ImageResult ready = image_ready_result(handle);
    if (ready.status == VFS_STATE_SUCCESS) {
        return ready;
    }

    // An absent/closed handle is a terminal error, not an in-flight request; record it so repeated queries
    // keep the same result.
    if (!fl_vfs_is_ready(vfs_handle)) {
        image_system_mark_load_failed(s_image_system, (FlImageLoadRequestId)handle);
        return (ImageResult) { .status = VFS_STATE_ERROR };
    }

    // Decode finished but the backend defers prepare_image (and so its realization ack) to the begin-frame pump.
    if (image_system_prepare_pending(s_image_system, (FlImageLoadRequestId)handle)) {
        return (ImageResult) { .status = VFS_STATE_PENDING };
    }

    // VFS ready, not deferred, and no realization ack landed - the decode or realization failed.
    log_error("image_get_data: VFS ready but not realized for handle=%u (vfs_status=0x%08x, core=0x%02x, sub=0x%04x)",
              (unsigned)handle, (unsigned)vfs_status, (unsigned)vfs_get_core_state(vfs_status),
              (unsigned)vfs_get_subsystem_error(vfs_status));
    image_system_mark_load_failed(s_image_system, (FlImageLoadRequestId)handle);
    fl_vfs_close(vfs_handle);
    return (ImageResult) { .status = VFS_STATE_ERROR };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageStatus image_get_status(ImageResult image) {
    if (vfs_is_pending(image.status)) {
        return ImageStatus_Pending;
    } else if (vfs_is_success(image.status)) {
        return ImageStatus_Ready;
    } else {
        return ImageStatus_Error;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlImageStatus image_public_status(ImageHandle handle) {
    if (handle == 0 || handle == UINT32_MAX) {
        return FlImageStatus_Error;
    }
    switch (image_get_status(image_get_data(handle))) {
        case ImageStatus_Pending:
            return FlImageStatus_Pending;
        case ImageStatus_Ready:
            return FlImageStatus_Ready;
        case ImageStatus_Error:
            return FlImageStatus_Error;
    }
    return FlImageStatus_Error;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageData* image_create_dynamic(u8* buffer, int width, int height, int stride, FlImageFormat format, FlArena* arena) {
    if (buffer == nullptr || arena == nullptr || width <= 0 || height <= 0 || stride <= 0) {
        return nullptr;
    }

    ImageData* image = arena_alloc_zero(arena, ImageData);
    image->data = buffer;
    image->width = width;
    image->height = height;
    image->padded_width = width;
    image->padded_height = height;
    image->stride = stride;
    image->channels = 4;
    image->format = format;

    return image;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlDynamicImage* dynamic_image_create(int width, int height, FlArena* arena) {
    if (arena == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    int stride = 0;
    size_t rgba_size = 0;
    if (!image_checked_stride(width, 4, &stride) || !image_checked_pixel_size(width, height, 4, &rgba_size)) {
        log_error("Dynamic image %dx%d is too large", width, height);
        return nullptr;
    }

    FlDynamicImage* image = arena_alloc_zero(arena, FlDynamicImage);
    image->width = width;
    image->height = height;
    image->user_stride = stride;
    image->is_alpha_only = false;
    image->user_buffer = arena_alloc_array(arena, u8, rgba_size);

    return image;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlDynamicImage* dynamic_image_create_alpha(int width, int height, FlArena* arena) {
    if (arena == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    size_t alpha_size = 0;
    if (!image_checked_pixel_size(width, height, 1, &alpha_size)) {
        log_error("Dynamic alpha image %dx%d is too large", width, height);
        return nullptr;
    }

    FlDynamicImage* image = arena_alloc_zero(arena, FlDynamicImage);
    image->width = width;
    image->height = height;
    image->user_stride = width;
    image->is_alpha_only = true;
    image->user_buffer = arena_alloc_array(arena, u8, alpha_size);

    return image;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_free(ImageHandle handle) {
    image_registry_remove(handle);
    image_cache_free_handle(handle);
    // The handle is the request id the backend keyed on at prepare_image (request_id == vfs_handle).
    image_system_release_image(s_image_system, (FlImageHandle)handle);
    image_system_clear_load_failed(s_image_system, (FlImageLoadRequestId)handle);
    image_system_forget_dedup(s_image_system, (FlImageLoadRequestId)handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void image_set_load_priority(ImageHandle handle, ImageLoadPriority priority) {
    if (handle == IMAGE_HANDLE_INVALID) {
        return;
    }

    // ImageLoadPriority and FlVfsLoadPriority share the same values.
    FlVfsLoadPriority vfs_priority = (FlVfsLoadPriority)priority;
    fl_vfs_set_priority((FlVfsHandle)handle, vfs_priority);
}
