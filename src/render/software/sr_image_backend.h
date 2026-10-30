#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Software render backend - static image realization (the image-preparation seam).
//
// Applies SW-specific transforms after decode and stores the result in the SW image cache:
//   - Bilinear padding (adds 1 pixel border for filtering)
//   - Deinterleaving (separate R,G,B,A planes for SIMD rendering)
//   - TLSF cache storage

#include <ui/image_decode.h>
#include <ui/image_system.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// prepare_image seam - realize a decoded static image into the SW cache (job-thread context).

void sw_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                      struct FlArena* scratch, const FlImageLoadRequest* request);
