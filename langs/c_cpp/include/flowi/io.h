
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "image.h"
#include "manual.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlIoApi;

struct FlIo;

// Load image from file/url. Supported formats are:
// JPEG baseline & progressive (12 bpc/arithmetic not supported, same as stock IJG lib)
// PNG 1/2/4/8/16-bit-per-channel
// TGA
// BMP non-1bpp, non-RLE
// PSD (composited view only, no extra channels, 8/16 bit-per-channel)
// GIF
// HDR (radiance rgbE format)
// PIC (Softimage PIC)
// PNM (PPM and PGM binary only)
static bool fl_io_load_image_from_url(struct FlIoApi* api, const char* filename);

#include "io.inl"

#ifdef __cplusplus
}
#endif
