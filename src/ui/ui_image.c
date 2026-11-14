#include "ui.h"
#include <core/arena.h>
#include "external/clay/clay.h"
#include "image/image.h"
#include "render_ir.h"
#include "ui_private.h"
#include <flowi/ui/image.h>

// Fill grows the element to its parent (which triggers scaled rendering); otherwise it is w x h pixels.
static Clay_Sizing image_sizing(bool fill, float w, float h) {
    if (fill) {
        return (Clay_Sizing) { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) };
    }
    return (Clay_Sizing) { .width = CLAY_SIZING_FIXED(w), .height = CLAY_SIZING_FIXED(h) };
}

// Emit a Clay image element for a prepared neutral image-source descriptor.
static void ui_emit_image_element(FlImageSource* source, FlColor color, Clay_Sizing sizing) {
    CLAY_AUTO_ID({ .backgroundColor = { color.r, color.g, color.b, 255 },
                   .layout = { .sizing = sizing },
                   .image = { .imageData = (void*)source } }) {}
}

void ui_image_impl(ImageHandle handle, FlImageOpts opts) {
    ImageResult data = image_get_data(handle);

    ImageStatus status = image_get_status(data);

    if (status != ImageStatus_Ready) {
        return;
    }

    // Clay's imageData slot carries the image-source descriptor; the Clay->IR translator reads it and the
    // backend resolves the handle to pixels on ingest.
    FlImageSource* source = arena_alloc_zero(g_ui_ctx->render_frame_allocator, FlImageSource);
    source->kind = 0;
    source->handle = (FlImageHandle)handle;

    ui_emit_image_element(source, opts.color, image_sizing(opts.fill_container, (float)data.width, (float)data.height));
}

void ui_dynamic_image_impl(FlDynamicImage* image, FlImageOpts opts) {
    if (image == nullptr) {
        return;
    }

    // The descriptor references the live RGBA buffer; the backend deinterleaves it on ingest.
    FlImageSource* source = arena_alloc_zero(g_ui_ctx->render_frame_allocator, FlImageSource);
    source->kind = 1;
    source->dynamic.pixels = image->user_buffer;
    source->dynamic.w = (u32)image->width;
    source->dynamic.h = (u32)image->height;
    source->dynamic.stride = (u32)image->user_stride;

    ui_emit_image_element(source, opts.color,
                          image_sizing(opts.fill_container, (float)image->width, (float)image->height));
}
