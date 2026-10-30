#include "sr_image_backend.h"
#include <core/arena.h>
#include <core/log.h>
#include <core/simd.h>
#include <ui/image_decode.h>
#include <ui/image_private.h>
#include <ui/image_registry.h>
#include <ui/image_system.h>
#include <core/memory.h>
#include "sr_image_convert.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adds one extra pixel at the end of each line and duplicates the last line, for bilinear filtering.
// Handles strided input data (after border detection).

static void pad_image_for_bilinear(InternalImageData* image_data, FlArena* arena) {
    if (!image_data->data || image_data->width <= 0 || image_data->height <= 0) {
        return;
    }

    int channels = image_data->channels;
    int input_stride = image_data->stride;
    int padded_stride = (image_data->width + 1) * channels;
    int padded_height = image_data->height + 1;

    size_t padded_size = padded_stride * padded_height;
    u8* padded_data = arena_alloc_array(arena, u8, padded_size);

    const u8* src = image_data->data;
    u8* dst = padded_data;

    for_count(y, image_data->height) {
        const u8* src_line = src + y * input_stride;
        u8* dst_line = dst + y * padded_stride;

        int pixels_to_copy = image_data->width;
        int bytes_to_copy = pixels_to_copy * channels;

        int simd_bytes = (bytes_to_copy / 16) * 16;
        for (int i = 0; i < simd_bytes; i += 16) {
            u8x16 src_chunk = u8x16_load_unaligned(src_line, i);
            u8x16_store_unaligned(src_chunk, dst_line, i);
        }

        for (int i = simd_bytes; i < bytes_to_copy; i++) {
            dst_line[i] = src_line[i];
        }

        // Duplicate last pixel of the line
        for_count(c, channels) {
            dst_line[bytes_to_copy + c] = src_line[bytes_to_copy - channels + c];
        }
    }

    // Duplicate the last line
    u8* last_dst_line = dst + image_data->height * padded_stride;
    memory_copy(last_dst_line, padded_stride, dst + (image_data->height - 1) * padded_stride, padded_stride);

    image_data->data = padded_data;
    image_data->padded_width = image_data->width + 1;
    image_data->padded_height = image_data->height + 1;
    image_data->stride = padded_stride;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Split interleaved pixels into separate R, G, B (and A) planes. Single-channel images are already planar.

static void deinterleave_image(InternalImageData* image_data, FlArena* arena) {
    int source_channels = image_data->channels;
    int pixel_count = image_data->padded_width * image_data->padded_height;

    if (source_channels == 1) {
        image_data->format = FlImageFormat_Deinterleaved;
        return;
    }

    u8* new_data = arena_alloc_array(arena, u8, pixel_count * source_channels);

    u8* r_buffer = new_data;
    u8* g_buffer = r_buffer + pixel_count;
    u8* b_buffer = g_buffer + pixel_count;
    u8* a_buffer = (source_channels == 4) ? b_buffer + pixel_count : NULL;

    sr_deinterleave_pixels(image_data->data, r_buffer, g_buffer, b_buffer, a_buffer, pixel_count, source_channels,
                           source_channels == 4);

    image_data->data = new_data;
    image_data->format = FlImageFormat_Deinterleaved;
    image_data->stride = image_data->padded_width * source_channels;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SW backend prepare_image seam. Runs on a job worker thread after image_decode() completes.

void sw_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                      FlArena* scratch, const FlImageLoadRequest* request) {
    (void)user_data;

    if (!decoded) {
        log_error("sw_prepare_image: decode was null for '%.*s' (request_id=%u)", (int)request->relative_path.length,
                  request->relative_path.data, (unsigned)request_id);
        return;
    }

    InternalImageData temp_data = {
        .data = decoded->pixels,
        .width = decoded->width,
        .height = decoded->height,
        .padded_width = decoded->width,
        .padded_height = decoded->height,
        .stride = decoded->stride,
        .channels = decoded->channels,
        .format = (decoded->channels == 1)   ? FlImageFormat_Alpha
                  : (decoded->channels == 3) ? FlImageFormat_RGB
                                             : FlImageFormat_RGBA,
        .status = VFS_STATE_SUCCESS,
    };

    pad_image_for_bilinear(&temp_data, scratch);

    // Regular images get deinterleaved, background images (Tint/Center) stay interleaved
    FlImageFormat target_format = FlImageFormat_Default;
    if (request->thumbnail_options.background_mode == ImageDecodeBackgroundMode_Regular) {
        target_format = FlImageFormat_Deinterleaved;
    }

    if (target_format == FlImageFormat_Deinterleaved) {
        deinterleave_image(&temp_data, scratch);
    }

    ImageCacheEntry* entry = image_cache_alloc_entry(&temp_data);
    if (!entry) {
        log_error("sw_prepare_image: cache alloc failed for '%.*s' (request_id=%u, %dx%d, ch=%d)",
                  (int)request->relative_path.length, request->relative_path.data, (unsigned)request_id,
                  temp_data.padded_width, temp_data.padded_height, temp_data.channels);
        return;
    }

    entry->public_data.format = temp_data.format;
    entry->public_data.width = temp_data.width;
    entry->public_data.height = temp_data.height;
    entry->public_data.channels = temp_data.channels;

    FlImageLoadOptions load_options = {
        .target_width = request->decode_options.target_width,
        .target_height = request->decode_options.target_height,
        .svg_scale_factor = request->decode_options.svg_scale_factor,
        .resize_filter = (request->decode_options.resize_filter == ImageDecodeResizeFilter_Nearest)
                             ? FlResizeFilter_Nearest
                             : FlResizeFilter_Linear,
        .background_mode = (request->thumbnail_options.background_mode == ImageDecodeBackgroundMode_Tint)
                               ? FlBackgroundMode_Tint
                               : FlBackgroundMode_Regular,
        .svg_preserve_aspect_ratio = request->decode_options.preserve_aspect_ratio,
        .detect_black_border = request->thumbnail_options.detect_black_border,
        .format = target_format,
    };
    entry->options = load_options;
    entry->status = VFS_STATE_SUCCESS;
    entry->mount = request->mount;
    entry->relative_path = image_cache_copy_string(request->relative_path);

    image_cache_mark_request_finished((ImageHandle)request_id, entry);

    // Readiness flips only once the pixels are resident in the SW pool.
    image_registry_mark_realized((ImageHandle)request_id, temp_data.width, temp_data.height);
}
