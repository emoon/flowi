#include "demo_assets.h"
#include <core/log.h>
#include <ui/image/image_decode.h>
#include <ui/image/image_system.h>

enum { IMAGE_W = 32, IMAGE_H = 32 };

static void fill_demo_pixels(u8* pixels) {
    for_count(y, IMAGE_H) {
        for_count(x, IMAGE_W) {
            u8* px = &pixels[(y * IMAGE_W + x) * 4];
            bool checker = (((x >> 3) ^ (y >> 3)) & 1) != 0;
            px[0] = checker ? (u8)(80 + x * 4) : 40;
            px[1] = (u8)(40 + y * 5);
            px[2] = checker ? 90 : (u8)(150 + x * 2);
            px[3] = 255;
        }
    }
}

// Builds a small procedural RGBA image and hands it to the backend's prepare_image under handle. The image is
// opaque, so premultiplied and straight alpha are the same. The GPU backend keys the static image slot on the
// request id, so handle is passed as the request id and referenced as an FlImageKind_Static source.
void prepare_demo_image(FlRenderBackend* backend, FlArena* scratch, FlImageHandle handle) {
    if (backend->prepare_image == nullptr) {
        return;
    }
    static u8 pixels[IMAGE_W * IMAGE_H * 4];
    fill_demo_pixels(pixels);
    FlImageDecodeResult decoded = {
        .pixels = pixels,
        .width = IMAGE_W,
        .height = IMAGE_H,
        .stride = IMAGE_W * 4,
        .channels = 4,
        .premultiplied = true,
    };
    FlImageLoadRequest request = { .relative_path = FL_S("procedural") };
    backend->prepare_image(backend->user_data, (FlImageLoadRequestId)handle, &decoded, scratch, &request);
}

SvgIcon load_svg_icon(FlArena* arena, SvgSource source, FlImageHandle handle) {
    SvgIcon icon = { .handle = handle };
    FlVectorImage* image = arena_alloc(arena, FlVectorImage);
    if (vector_image_parse(source.data, source.size, arena, image)) {
        icon.image = image;
    } else {
        log_error("%s failed to parse", source.name);
    }
    return icon;
}
