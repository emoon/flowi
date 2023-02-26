typedef struct FlIoApi {
    struct FlInternalData* priv;
    FlImage (*load_image_from_url)(struct FlInternalData* priv, FlString filename);
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
FL_INLINE FlImage fl_io_load_image_from_url(struct FlIoApi* api, const char* filename) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->load_image_from_url)(api->priv, filename_);
}
