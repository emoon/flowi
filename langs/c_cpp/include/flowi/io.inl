typedef struct FlIoApi {
    struct FlInternalData* priv;
    FlShaderProgram (*load_shader_program_comp)(struct FlInternalData* priv, FlString vs_filename,
                                                FlString ps_filename);
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
// Load a vertex shader and pixel shader to be used as a shader program. This will also compile the shaders.
FL_INLINE FlShaderProgram fl_io_load_shader_program_comp(const char* vs_filename, const char* ps_filename) {
    FlString vs_filename_ = fl_cstr_to_flstring(vs_filename);
    FlString ps_filename_ = fl_cstr_to_flstring(ps_filename);
#ifdef FLOWI_STATIC

        return fl_io_load_shader_program_comp_impl(void* ctx, vs_filename_, ps_filename_);
#else
    return (api->load_shader_program_comp)(void* ctx, vs_filename_, ps_filename_);
#endif
}
