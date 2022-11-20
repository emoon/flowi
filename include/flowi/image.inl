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
FlImage fl_image_create_from_file_impl(struct FlContext* ctx, FlString filename);

FL_INLINE FlImage fl_image_create_from_file(struct FlContext* ctx, const char* filename) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return fl_image_create_from_file_impl(ctx, filename_);
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
FlImage fl_image_create_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size);

FL_INLINE FlImage fl_image_create_from_memory(struct FlContext* ctx, const char* name, uint8_t* data,
                                              uint32_t data_size) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_image_create_from_memory_impl(ctx, name_, data, data_size);
}

// Load SVG from file
FlImage fl_image_create_svg_from_file_impl(struct FlContext* ctx, FlString filename, uint32_t target_width,
                                           FlSvgFlags format);

FL_INLINE FlImage fl_image_create_svg_from_file(struct FlContext* ctx, const char* filename, uint32_t target_width,
                                                FlSvgFlags format) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return fl_image_create_svg_from_file_impl(ctx, filename_, target_width, format);
}

// Load SVG from memory
FlImage fl_image_create_svg_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                             uint32_t target_width, FlSvgFlags format);

FL_INLINE FlImage fl_image_create_svg_from_memory(struct FlContext* ctx, const char* name, uint8_t* data,
                                                  uint32_t data_size, uint32_t target_width, FlSvgFlags format) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_image_create_svg_from_memory_impl(ctx, name_, data, data_size, target_width, format);
}

// Get data amout the image
FlImageInfo* fl_image_get_info_impl(struct FlContext* ctx, FlImage image);

FL_INLINE FlImageInfo* fl_image_get_info(struct FlContext* ctx, FlImage image) {
    return fl_image_get_info_impl(ctx, image);
}

// Destroy the created image
void fl_image_destroy_impl(struct FlContext* ctx, FlImage self);

FL_INLINE void fl_image_destroy(struct FlContext* ctx, FlImage self) {
    fl_image_destroy_impl(ctx, self);
}
