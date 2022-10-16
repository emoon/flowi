// Create a font from (TTF) file. To use the font use [ui::set_font] before using text-based widgets
// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FlFont fl_font_new_from_file_impl(struct FlContext* ctx, FlString filename, uint32_t font_size);

FL_INLINE FlFont fl_font_new_from_file(struct FlContext* ctx, const char* filename, uint32_t font_size) {
    FlString filename_ = fl_cstr_to_flstring(filename);
    return fl_font_new_from_file_impl(ctx, filename_, font_size);
}

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FlFont fl_font_new_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                    uint32_t font_size);

FL_INLINE FlFont fl_font_new_from_memory(struct FlContext* ctx, const char* name, uint8_t* data, uint32_t data_size,
                                         uint32_t font_size) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_font_new_from_memory_impl(ctx, name_, data, data_size, font_size);
}

// Destory the current font, render the id invalid
void fl_font_destroy_impl(struct FlContext* ctx, FlFont font);

FL_INLINE void fl_font_destroy(struct FlContext* ctx, FlFont font) {
    fl_font_destroy_impl(ctx, font);
}
