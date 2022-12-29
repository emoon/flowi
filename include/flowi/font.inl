typedef struct FlFontApi {
    struct FlContext* ctx;
    FlFont (*new_from_file)(struct FlContext* ctx, FlString filename, uint32_t font_size);
    FlFont (*new_from_memory)(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size,
                              uint32_t font_size);
    void (*destroy)(struct FlContext* ctx, FlFont font);
} FlFontApi;

// Create a font from (TTF) file. To use the font use [ui::set_font] before using text-based widgets
// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FL_INLINE FlFont fl_font_new_from_file(struct FlFontApi* api, const char* filename, uint32_t font_size) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return (api->new_from_file)(api->ctx, filename_, font_size);
}

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FL_INLINE FlFont fl_font_new_from_memory(struct FlFontApi* api, const char* name, uint8_t* data, uint32_t data_size,
                                         uint32_t font_size) {
    FlString name_ = fl_cstr_to_flstring(name);
    return (api->new_from_memory)(api->ctx, name_, data, data_size, font_size);
}

// Destory the current font, render the id invalid
FL_INLINE void fl_font_destroy(struct FlFontApi* api, FlFont font) {
    (api->destroy)(api->ctx, font);
}
