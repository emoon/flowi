typedef struct FlIoApi {
    struct FlInternalData* priv;
    FlImage (*load_image)(struct FlInternalData* priv, FlString filename);
    FlImage (*load_image_async)(struct FlInternalData* priv, FlString filename, FlLoaderType loader_type);
    FlImage (*load_file)(struct FlInternalData* priv, FlString filename, FlLoaderType loader_type);
    FlShaderProgram (*load_shader_program_comp)(struct FlInternalData* priv, FlString vs_filename,
                                                FlString ps_filename);
} FlIoApi;

extern FlIoApi* g_flowi_io_api;

#ifdef FLOWI_STATIC
FlImage fl_io_load_image_impl(struct FlInternalData* priv, FlString filename);
FlImage fl_io_load_image_async_impl(struct FlInternalData* priv, FlString filename, FlLoaderType loader_type);
FlImage fl_io_load_file_impl(struct FlInternalData* priv, FlString filename, FlLoaderType loader_type);
FlShaderProgram fl_io_load_shader_program_comp_impl(struct FlInternalData* priv, FlString vs_filename,
                                                    FlString ps_filename);
#endif

// Load image from file/url. Supported formats are: JPG, PNG, WEBP
FL_INLINE FlImage fl_io_load_image(const char* filename) {
    FlString filename_ = fl_cstr_to_flstring(filename);
#ifdef FLOWI_STATIC
    return fl_io_load_image_impl(g_flowi_io_api->priv, filename_);
#else
    return (g_flowi_io_api->load_image)(g_flowi_io_api->priv, filename_);
#endif
}

// Load image async from file/url. Supported formats are: JPG, PNG, WEBP
FL_INLINE FlImage fl_io_load_image_async(const char* filename, FlLoaderType loader_type) {
    FlString filename_ = fl_cstr_to_flstring(filename);
#ifdef FLOWI_STATIC
    return fl_io_load_image_async_impl(g_flowi_io_api->priv, filename_, loader_type);
#else
    return (g_flowi_io_api->load_image_async)(g_flowi_io_api->priv, filename_, loader_type);
#endif
}

// Load image from file/url
FL_INLINE FlImage fl_io_load_file(const char* filename, FlLoaderType loader_type) {
    FlString filename_ = fl_cstr_to_flstring(filename);
#ifdef FLOWI_STATIC
    return fl_io_load_file_impl(g_flowi_io_api->priv, filename_, loader_type);
#else
    return (g_flowi_io_api->load_file)(g_flowi_io_api->priv, filename_, loader_type);
#endif
}

// Same as load_image_from_url, but async and gives back a handle to check/access data later.
// Load a vertex shader be used for rendering. This will also compile the shader.
// Load a pixel shader to be used for rendering. This will also compile the shader.
// Load a vertex shader and pixel shader to be used as a shader program. This will also compile the shaders.
FL_INLINE FlShaderProgram fl_io_load_shader_program_comp(const char* vs_filename, const char* ps_filename) {
    FlString vs_filename_ = fl_cstr_to_flstring(vs_filename);
    FlString ps_filename_ = fl_cstr_to_flstring(ps_filename);
#ifdef FLOWI_STATIC
    return fl_io_load_shader_program_comp_impl(g_flowi_io_api->priv, vs_filename_, ps_filename_);
#else
    return (g_flowi_io_api->load_shader_program_comp)(g_flowi_io_api->priv, vs_filename_, ps_filename_);
#endif
}
