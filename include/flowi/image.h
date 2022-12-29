
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FlSvgFlags {
    // Render the SVG image using RGBA format
    FlSvgFlags_Rgba = 0,
    // Render the SVG image using Alpha only
    FlSvgFlags_Alpha = 1,
} FlSvgFlags;

typedef struct FlImageInfo {
    // width of the image
    uint32_t width;
    // height of the Image
    uint32_t height;
} FlImageInfo;

struct FlImageApi;

typedef uint64_t FlImage;

// Load image from file. Supported formats are:
// JPEG baseline & progressive (12 bpc/arithmetic not supported, same as stock IJG lib)
// PNG 1/2/4/8/16-bit-per-channel
// TGA
// BMP non-1bpp, non-RLE
// PSD (composited view only, no extra channels, 8/16 bit-per-channel)
// GIF
// HDR (radiance rgbE format)
// PIC (Softimage PIC)
// PNM (PPM and PGM binary only)
static FlImage fl_image_create_from_file(struct FlImageApi* api, const char* filename);

// Load image from memory. Supported formats are:
// JPEG baseline & progressive (12 bpc/arithmetic not supported, same as stock IJG lib)
// PNG 1/2/4/8/16-bit-per-channel
// TGA
// BMP non-1bpp, non-RLE
// PSD (composited view only, no extra channels, 8/16 bit-per-channel)
// GIF
// HDR (radiance rgbE format)
// PIC (Softimage PIC)
// PNM (PPM and PGM binary only)
static FlImage fl_image_create_from_memory(struct FlImageApi* api, const char* name, uint8_t* data, uint32_t data_size);

// Load SVG from file
static FlImage fl_image_create_svg_from_file(struct FlImageApi* api, const char* filename, uint32_t target_width,
                                             FlSvgFlags flags);

// Load SVG from memory
static FlImage fl_image_create_svg_from_memory(struct FlImageApi* api, const char* name, uint8_t* data,
                                               uint32_t data_size, uint32_t target_width, FlSvgFlags flags);

// Get data amout the image
static FlImageInfo* fl_image_get_info(struct FlImageApi* api, FlImage image);

// Destroy the created image
static void fl_image_destroy(struct FlImageApi* api, FlImage self);

#include "image.inl"

#ifdef __cplusplus
}
#endif
