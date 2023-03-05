typedef struct FlIoApi {
    struct FlInternalData* priv;
    FlShader (*load_fragment_shader_comp)(struct FlInternalData* priv, FlString filename);
} FlIoApi;

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
FL_INLINE FlShader fl_io_load_fragment_shader_comp(struct FlIoApi* api, const char* filename) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->load_fragment_shader_comp)(api->priv, filename_);
}
