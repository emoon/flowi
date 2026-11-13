#pragma once

// Raw-pixel images: caller-supplied pixels in an FlImageFormat, converted into the decode result the backend realizes.

#include <core/arena.h>
#include <flowi/ui/image.h>
#include "image_decode.h"

// The creation shape of an image built from raw pixels, so an update can re-specify it from a handle alone.
typedef struct RawPixelShape {
    int width;
    int height;
    FlImageFormat format;
    bool dedicated;
} RawPixelShape;

// Copies pixels into arena memory as a final (premultiplied RGBA, or alpha-only) decode result. Returns false for a
// shape or format that cannot be converted, or one whose size does not fit.
bool image_raw_pixels_to_decoded(FlArena* arena, const void* pixels, RawPixelShape shape, FlImageDecodeResult* out);
