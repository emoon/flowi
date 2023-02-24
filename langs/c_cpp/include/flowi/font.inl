typedef struct FlFontApi {
    struct FlInternalData* priv;
    FlFont (*new_from_file)(struct FlInternalData* priv, FlString filename, uint32_t font_size);
    FlFont (*new_from_file_range)(struct FlInternalData* priv, FlString filename, uint32_t font_size,
                                  uint16_t glyph_range_start, uint16_t glyph_range_end);
    FlFont (*new_from_memory)(struct FlInternalData* priv, FlString name, uint8_t* data, uint32_t data_size,
                              uint32_t font_size);
    void (*push)(struct FlInternalData* priv, FlFont font);
    void (*pop)(struct FlInternalData* priv);
    void (*destroy)(struct FlInternalData* priv, FlFont font);
} FlFontApi;

// Create a font from (TTF) file. To use the font use [ui::set_font] before using text-based widgets
// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FL_INLINE FlFont fl_font_new_from_file(struct FlFontApi* api, const char* filename, uint32_t font_size) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->new_from_file)(api->priv, filename_, font_size);
}

// Create an new font from a FFT file with a range of characters that should be pre-generated
FL_INLINE FlFont fl_font_new_from_file_range(struct FlFontApi* api, const char* filename, uint32_t font_size,
                                             uint16_t glyph_range_start, uint16_t glyph_range_end) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->new_from_file_range)(api->priv, filename_, font_size, glyph_range_start, glyph_range_end);
}

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FL_INLINE FlFont fl_font_new_from_memory(struct FlFontApi* api, const char* name, uint8_t* data, uint32_t data_size,
                                         uint32_t font_size) {
    FlString name_ = fl_cstr_to_flstring(name);
    return (api->new_from_memory)(api->priv, name_, data, data_size, font_size);
}

// Push a font for usage
FL_INLINE void fl_font_push(struct FlFontApi* api, FlFont font) {
    (api->push)(api->priv, font);
}

// Pop a font from the stack
FL_INLINE void fl_font_pop(struct FlFontApi* api) {
    (api->pop)(api->priv);
}

// Destory the current font, render the id invalid
FL_INLINE void fl_font_destroy(struct FlFontApi* api, FlFont font) {
    (api->destroy)(api->priv, font);
}
