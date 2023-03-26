typedef struct FlImageApi {
    struct FlInternalData* priv;
    FlImageInfo* (*get_info)(struct FlInternalData* priv, FlImage image);
} FlImageApi;

extern FlImageApi* g_flowi_image_api;

#ifdef FLOWI_STATIC
FlImageInfo* fl_image_get_info_impl(struct FlInternalData* priv, FlImage image);
#endif

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
// Load SVG from file
// Load SVG from memory
// Get data amout the image
FL_INLINE FlImageInfo* fl_image_get_info(FlImage image) {
#ifdef FLOWI_STATIC
    return fl_image_get_info_impl(g_flowi_image_api->priv, image);
#else
    return (g_flowi_image_api->get_info)(g_flowi_image_api->priv, image);
#endif
}
