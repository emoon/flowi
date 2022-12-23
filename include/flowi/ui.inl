// Start a window
bool fl_ui_window_begin_impl(struct FlContext* ctx, FlString name, FlWindowFlags flags);

FL_INLINE bool fl_ui_window_begin(struct FlContext* ctx, const char* name, FlWindowFlags flags) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_ui_window_begin_impl(ctx, name_, flags);
}

// End call for various types such as windows, lists, etc.
void fl_ui_end_impl(struct FlContext* ctx);

FL_INLINE void fl_ui_end(struct FlContext* ctx) {
    fl_ui_end_impl(ctx);
}

// Draw static text with the selected font
void fl_ui_text_impl(struct FlContext* ctx, FlString text);

FL_INLINE void fl_ui_text(struct FlContext* ctx, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    fl_ui_text_impl(ctx, text_);
}

// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
void fl_ui_image_impl(struct FlContext* ctx, FlImage image);

FL_INLINE void fl_ui_image(struct FlContext* ctx, FlImage image) {
    fl_ui_image_impl(ctx, image);
}

// Draw image with given size
void fl_ui_image_with_size_impl(struct FlContext* ctx, FlImage image, FlVec2 size);

FL_INLINE void fl_ui_image_with_size(struct FlContext* ctx, FlImage image, FlVec2 size) {
    fl_ui_image_with_size_impl(ctx, image, size);
}

// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos);

FL_INLINE void fl_ui_set_pos(struct FlContext* ctx, FlVec2 pos) {
    fl_ui_set_pos_impl(ctx, pos);
}

// Get the last widget size. This is usually used for doing manual layouting
FlRect fl_ui_get_last_widget_size_impl(struct FlContext* ctx, FlVec2 pos);

FL_INLINE FlRect fl_ui_get_last_widget_size(struct FlContext* ctx, FlVec2 pos) {
    return fl_ui_get_last_widget_size_impl(ctx, pos);
}

// Push button widget that returns true if user has pressed it
bool fl_ui_push_button_with_icon_impl(struct FlContext* ctx, FlString text, FlImage image, FlVec2 text_pos,
                                      float image_scale);

FL_INLINE bool fl_ui_push_button_with_icon(struct FlContext* ctx, const char* text, FlImage image, FlVec2 text_pos,
                                           float image_scale) {
    FlString text_ = fl_cstr_to_flstring(text);
    return fl_ui_push_button_with_icon_impl(ctx, text_, image, text_pos, image_scale);
}

// Push button widget that returns true if user has pressed it
bool fl_ui_push_button_impl(struct FlContext* ctx, FlString text);

FL_INLINE bool fl_ui_push_button(struct FlContext* ctx, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    return fl_ui_push_button_impl(ctx, text_);
}