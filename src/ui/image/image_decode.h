#pragma once

// Image Decode - Pure decode and common processing
//
// Synchronous, backend-agnostic image decoding: no storage, no VFS async, no renderer-specific transforms.
// The output is RGBA interleaved (or Alpha-only for grayscale), ready for backend-specific processing
// (SW: deinterleave + bilinear padding, GPU: atlas packing + upload).

#include <core/arena.h>
#include <core/core.h>

// Decode result - temporary, allocated from provided arena

typedef struct FlImageDecodeResult {
    u8* pixels; // RGBA interleaved, or Alpha-only (1 channel)
    i32 width;
    i32 height;
    i32 stride;  // Bytes per row
    u8 channels; // 4 (RGBA) or 1 (grayscale/alpha)
    bool premultiplied;

    // True for raster decodes that are still raw (not premultiplied, not resized): they need
    // image_thumbnail_compose() to run the finishing pass. SVG and waveform decodes leave it false.
    bool needs_thumbnail_compose;
} FlImageDecodeResult;

// Decode options

typedef enum ImageDecodeResizeFilter {
    ImageDecodeResizeFilter_Default = 0,
    ImageDecodeResizeFilter_Nearest,
} ImageDecodeResizeFilter;

typedef enum ImageDecodeBackgroundMode {
    ImageDecodeBackgroundMode_Regular = 0,
    ImageDecodeBackgroundMode_Tint,
    ImageDecodeBackgroundMode_Center,
} ImageDecodeBackgroundMode;

typedef struct ImageDecodeOptions {
    i32 target_width;     // 0 = no resize (SVG rasterizes at this size; raster resizes in compose)
    i32 target_height;    // 0 = no resize
    f32 svg_scale_factor; // For SVG rasterization (default 1.0)
    f32 ui_scale;         // UI scale factor (for resolution-aware loading)
    ImageDecodeResizeFilter resize_filter;
    bool preserve_aspect_ratio;
    bool detect_grayscale; // Convert grayscale RGBA to single channel
    bool force_rgb;        // Decode raster as 3-channel RGB (drop alpha)
} ImageDecodeOptions;

// Thumbnail compose options
//
// Fed to image_thumbnail_compose() after image_decode(); the caller supplies any theme-derived colors (tint).

typedef struct ImageThumbnailOptions {
    ImageDecodeBackgroundMode background_mode;
    bool detect_black_border; // Detect and remove black borders (before resize)

    // Tint color (used when background_mode == Tint) - caller fills from the active theme
    u8 tint_r, tint_g, tint_b;

    // Vertical fade gradient: fades from original image (top) to target color (bottom)
    bool apply_vertical_fade;
    f32 fade_start; // Where fade starts (0.0 = top, 1.0 = bottom)
    u8 fade_r, fade_g, fade_b;
} ImageThumbnailOptions;

// Decode API

// Decode an image from a memory buffer (synchronous). Allocates result and pixels from arena; returns NULL on
// failure. Detects SVG vs raster and decodes via nanosvg or stb_image:
// - SVG/waveform: rasterizes at target size, premultiplies, detects grayscale (final)
// - Raster: returns the raw decoded pixels (not premultiplied, not resized) with needs_thumbnail_compose set
FlImageDecodeResult* image_decode(FlArena* arena, const void* file_data, size_t file_size,
                                  const ImageDecodeOptions* options);

// Thumbnail compose stage
//
// Run after image_decode() on the same arena. Finishes a raw raster decode in place: black-border crop,
// premultiply, resize to target, vertical fade, tint/letterbox compositing, and grayscale conversion. No-op for
// results that do not have needs_thumbnail_compose set.
//
// decode_opts supplies the resize geometry (target size, aspect, filter, ui scale) and detect_grayscale;
// thumb supplies the compositing parameters, including caller-provided theme colors.
void image_thumbnail_compose(FlArena* arena, FlImageDecodeResult* result, const ImageDecodeOptions* decode_opts,
                             const ImageThumbnailOptions* thumb);

// Checked pixel sizes
//
// Every width*height*bytes_per_pixel allocation or pixel count goes through these.

// Byte size of a width x height image at bytes_per_pixel. Returns false (leaving out_size untouched) for a
// non-positive dimension or a size that does not fit size_t.
bool image_checked_pixel_size(int width, int height, size_t bytes_per_pixel, size_t* out_size);

// Row stride in bytes for width pixels at bytes_per_pixel. Returns false when it does not fit the i32 stride fields.
bool image_checked_stride(int width, size_t bytes_per_pixel, int* out_stride);

// Premultiply

// round(c * a / 255) for 8-bit inputs, as (t + (t >> 8)) >> 8 with t = c * a + 128.
static inline u8 image_premul_channel(int c, int a) {
    const int t = c * a + 128;
    return (u8)((t + (t >> 8)) >> 8);
}

// Shared with image_thumbnail.c

typedef struct ImageSize {
    int width, height;
} ImageSize;

// True when the options ask for a target size (SVG rasterizes at it, rasters resize to it in compose).
bool image_has_target_size(const ImageDecodeOptions* opts);

// The target size multiplied by ui_scale and rounded.
ImageSize image_scaled_target_size(const ImageDecodeOptions* opts);

// Collapses an RGBA image whose color is all gray into a single alpha channel; a no-op for anything else.
void image_convert_grayscale_to_alpha(FlImageDecodeResult* image, FlArena* arena);

// Compose primitives

typedef struct Rgb8 {
    u8 r, g, b;
} Rgb8;

// A 3-channel canvas plus where the source image lands on it. Offsets may be negative; the blit is clamped.
typedef struct ImageCanvas {
    u8* pixels; // width * height * 3 bytes
    i32 width;
    i32 height;
    Rgb8 fill;
    i32 offset_x;
    i32 offset_y;
} ImageCanvas;

// Compose primitive behind the Tint/Center background modes: fill the canvas and blit image onto it. Reads are
// channel-agnostic (1-/2-channel grayscale sources replicate the gray value into R/G/B) and the blit is clamped to
// the canvas, so an overhanging source is cropped.
void composite_source_into_canvas_rgb(const FlImageDecodeResult* image, const ImageCanvas* canvas);

// Half-open pixel rectangle: rows [row_start, row_end), columns [col_start, col_end).
typedef struct PixelRect {
    int row_start, row_end;
    int col_start, col_end;
} PixelRect;

// True when every pixel in region has zero colour bytes. Alpha is ignored; an empty region is all black.
bool image_region_all_black(const FlImageDecodeResult* image, PixelRect region);
