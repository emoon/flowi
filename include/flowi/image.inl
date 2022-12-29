typedef struct FlImageApi {
    struct FlContext* ctx;
    FlImage (*create_from_file)(struct FlContext* ctx, FlString filename);
    FlImage (*create_from_memory)(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size);
    FlImage (*create_svg_from_file)(struct FlContext* ctx, FlString filename, uint32_t target_width, FlSvgFlags flags);
    FlImage (*create_svg_from_memory)(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                      uint32_t target_width, FlSvgFlags flags);
    FlImageInfo* (*get_info)(struct FlContext* ctx, FlImage image);
    void (*destroy)(struct FlContext* ctx, FlImage self);
} FlImageApi;

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
FL_INLINE FlImage fl_image_create_from_file(struct FlImageApi* api, const char* filename) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->create_from_file)(api->ctx, filename_);
}

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
FL_INLINE FlImage fl_image_create_from_memory(struct FlImageApi* api, const char* name, uint8_t* data,
                                              uint32_t data_size) {
    FlString name_ = fl_cstr_to_flstring(name);
    return (api->create_from_memory)(api->ctx, name_, data, data_size);
}

// Load SVG from file
FL_INLINE FlImage fl_image_create_svg_from_file(struct FlImageApi* api, const char* filename, uint32_t target_width,
                                                FlSvgFlags flags) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->create_svg_from_file)(api->ctx, filename_, target_width, flags);
}

// Load SVG from memory
FL_INLINE FlImage fl_image_create_svg_from_memory(struct FlImageApi* api, const char* name, uint8_t* data,
                                                  uint32_t data_size, uint32_t target_width, FlSvgFlags flags) {
    FlString name_ = fl_cstr_to_flstring(name);
    return (api->create_svg_from_memory)(api->ctx, name_, data, data_size, target_width, flags);
}

// Get data amout the image
FL_INLINE FlImageInfo* fl_image_get_info(struct FlImageApi* api, FlImage image) {
    return (api->get_info)(api->ctx, image);
}

// Destroy the created image
FL_INLINE void fl_image_destroy(struct FlImageApi* api, FlImage self) {
    (api->destroy)(api->ctx, self);
}
