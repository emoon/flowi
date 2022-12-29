typedef struct FlUiApi {
    struct FlContext* ctx;
    bool (*window_begin)(struct FlContext* ctx, FlString name, FlWindowFlags flags);
    void (*end)(struct FlContext* ctx);
    void (*text)(struct FlContext* ctx, FlString text);
    void (*image)(struct FlContext* ctx, FlImage image);
    void (*image_with_size)(struct FlContext* ctx, FlImage image, FlVec2 size);
    void (*set_pos)(struct FlContext* ctx, FlVec2 pos);
    FlRect (*get_last_widget_size)(struct FlContext* ctx, FlVec2 pos);
    bool (*push_button_with_icon)(struct FlContext* ctx, FlString text, FlImage image, FlVec2 text_pos,
                                  float image_scale);
    bool (*push_button)(struct FlContext* ctx, FlString text);
} FlUiApi;

// Start a window
FL_INLINE bool fl_ui_window_begin(struct FlUiApi* api, const char* name, FlWindowFlags flags) {
    FlString name_ = fl_cstr_to_flstring(name);
    return (api->window_begin)(api->ctx, name_, flags);
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_ui_end(struct FlUiApi* api) {
    (api->end)(api->ctx);
}

// Draw static text with the selected font
FL_INLINE void fl_ui_text(struct FlUiApi* api, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    (api->text)(api->ctx, text_);
}

// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
FL_INLINE void fl_ui_image(struct FlUiApi* api, FlImage image) {
    (api->image)(api->ctx, image);
}

// Draw image with given size
FL_INLINE void fl_ui_image_with_size(struct FlUiApi* api, FlImage image, FlVec2 size) {
    (api->image_with_size)(api->ctx, image, size);
}

// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
FL_INLINE void fl_ui_set_pos(struct FlUiApi* api, FlVec2 pos) {
    (api->set_pos)(api->ctx, pos);
}

// Get the last widget size. This is usually used for doing manual layouting
FL_INLINE FlRect fl_ui_get_last_widget_size(struct FlUiApi* api, FlVec2 pos) {
    return (api->get_last_widget_size)(api->ctx, pos);
}

// Push button widget that returns true if user has pressed it
FL_INLINE bool fl_ui_push_button_with_icon(struct FlUiApi* api, const char* text, FlImage image, FlVec2 text_pos,
                                           float image_scale) {
    FlString text_ = fl_cstr_to_flstring(text);
    return (api->push_button_with_icon)(api->ctx, text_, image, text_pos, image_scale);
}

// Push button widget that returns true if user has pressed it
FL_INLINE bool fl_ui_push_button(struct FlUiApi* api, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    return (api->push_button)(api->ctx, text_);
}
