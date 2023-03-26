typedef struct FlUiApi {
    struct FlInternalData* priv;
    void (*text)(struct FlInternalData* priv, FlString text);
    void (*image)(struct FlInternalData* priv, FlImage image);
    void (*image_with_size)(struct FlInternalData* priv, FlImage image, FlVec2 size);
    void (*set_pos)(struct FlInternalData* priv, FlVec2 pos);
    FlRect (*get_last_widget_size)(struct FlInternalData* priv, FlVec2 pos);
    bool (*push_button_with_icon)(struct FlInternalData* priv, FlString text, FlImage image, FlVec2 text_pos,
                                  float image_scale);
    bool (*push_button)(struct FlInternalData* priv, FlString text);
} FlUiApi;

extern FlUiApi* g_flowi_ui_api;

#ifdef FLOWI_STATIC
void fl_ui_text_impl(struct FlInternalData* priv, FlString text);
void fl_ui_image_impl(struct FlInternalData* priv, FlImage image);
void fl_ui_image_with_size_impl(struct FlInternalData* priv, FlImage image, FlVec2 size);
void fl_ui_set_pos_impl(struct FlInternalData* priv, FlVec2 pos);
FlRect fl_ui_get_last_widget_size_impl(struct FlInternalData* priv, FlVec2 pos);
bool fl_ui_push_button_with_icon_impl(struct FlInternalData* priv, FlString text, FlImage image, FlVec2 text_pos,
                                      float image_scale);
bool fl_ui_push_button_impl(struct FlInternalData* priv, FlString text);
#endif

// Draw static text with the selected font
FL_INLINE void fl_ui_text(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    fl_ui_text_impl(g_flowi_ui_api->priv, text_);
#else
    (g_flowi_ui_api->text)(g_flowi_ui_api->priv, text_);
#endif
}

// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
FL_INLINE void fl_ui_image(FlImage image) {
#ifdef FLOWI_STATIC
    fl_ui_image_impl(g_flowi_ui_api->priv, image);
#else
    (g_flowi_ui_api->image)(g_flowi_ui_api->priv, image);
#endif
}

// Draw image with given size
FL_INLINE void fl_ui_image_with_size(FlImage image, FlVec2 size) {
#ifdef FLOWI_STATIC
    fl_ui_image_with_size_impl(g_flowi_ui_api->priv, image, size);
#else
    (g_flowi_ui_api->image_with_size)(g_flowi_ui_api->priv, image, size);
#endif
}

// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
FL_INLINE void fl_ui_set_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC
    fl_ui_set_pos_impl(g_flowi_ui_api->priv, pos);
#else
    (g_flowi_ui_api->set_pos)(g_flowi_ui_api->priv, pos);
#endif
}

// Get the last widget size. This is usually used for doing manual layouting
FL_INLINE FlRect fl_ui_get_last_widget_size(FlVec2 pos) {
#ifdef FLOWI_STATIC
    return fl_ui_get_last_widget_size_impl(g_flowi_ui_api->priv, pos);
#else
    return (g_flowi_ui_api->get_last_widget_size)(g_flowi_ui_api->priv, pos);
#endif
}

// Push button widget that returns true if user has pressed it
FL_INLINE bool fl_ui_push_button_with_icon(const char* text, FlImage image, FlVec2 text_pos, float image_scale) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    return fl_ui_push_button_with_icon_impl(g_flowi_ui_api->priv, text_, image, text_pos, image_scale);
#else
    return (g_flowi_ui_api->push_button_with_icon)(g_flowi_ui_api->priv, text_, image, text_pos, image_scale);
#endif
}

// Push button widget that returns true if user has pressed it
FL_INLINE bool fl_ui_push_button(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    return fl_ui_push_button_impl(g_flowi_ui_api->priv, text_);
#else
    return (g_flowi_ui_api->push_button)(g_flowi_ui_api->priv, text_);
#endif
}
