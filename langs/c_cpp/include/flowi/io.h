
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "image.h"
#include "manual.h"
#include "shader.h"

#ifdef __cplusplus
extern "C" {
#endif

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
// Same as load_image_from_url, but async and gives back a handle to check/access data later.
// Load a vertex shader be used for rendering. This will also compile the shader.
// Load a pixel shader to be used for rendering. This will also compile the shader.
// Load a vertex shader and pixel shader to be used as a shader program. This will also compile the shaders.
static FlShaderProgram fl_io_load_shader_program_comp(const char* vs_filename, const char* ps_filename);

#include "io.inl"

#ifdef __cplusplus
}
#endif
