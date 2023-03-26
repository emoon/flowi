typedef struct FlButtonApi {
    struct FlInternalData* priv;
    bool (*regular)(struct FlInternalData* priv, FlString label);
    bool (*regular_size)(struct FlInternalData* priv, FlString label, FlVec2 size);
    bool (*small)(struct FlInternalData* priv, FlString label);
    bool (*invisible)(struct FlInternalData* priv, FlString label, FlVec2 size, FlButtonFlags flags);
    bool (*check_box)(struct FlInternalData* priv, FlString label, bool* state);
    bool (*radio)(struct FlInternalData* priv, FlString label, bool state);
    void (*bullet)(struct FlInternalData* priv);
    bool (*image_with_text)(struct FlInternalData* priv, FlImage image, FlString label);
} FlButtonApi;

extern FlButtonApi* g_flowi_button_api;

#ifdef FLOWI_STATIC
bool fl_button_regular_impl(struct FlInternalData* priv, FlString label);
bool fl_button_regular_size_impl(struct FlInternalData* priv, FlString label, FlVec2 size);
bool fl_button_small_impl(struct FlInternalData* priv, FlString label);
bool fl_button_invisible_impl(struct FlInternalData* priv, FlString label, FlVec2 size, FlButtonFlags flags);
bool fl_button_check_box_impl(struct FlInternalData* priv, FlString label, bool* state);
bool fl_button_radio_impl(struct FlInternalData* priv, FlString label, bool state);
void fl_button_bullet_impl(struct FlInternalData* priv);
bool fl_button_image_with_text_impl(struct FlInternalData* priv, FlImage image, FlString label);
#endif

// Show a regular push button
FL_INLINE bool fl_button_regular(const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_regular_impl(g_flowi_button_api->priv, label_);
#else
    return (g_flowi_button_api->regular)(g_flowi_button_api->priv, label_);
#endif
}

// Show a regular push button with a specific size
FL_INLINE bool fl_button_regular_size(const char* label, FlVec2 size) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_regular_size_impl(g_flowi_button_api->priv, label_, size);
#else
    return (g_flowi_button_api->regular_size)(g_flowi_button_api->priv, label_, size);
#endif
}

// Show a regular push button without any frame padding.
FL_INLINE bool fl_button_small(const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_small_impl(g_flowi_button_api->priv, label_);
#else
    return (g_flowi_button_api->small)(g_flowi_button_api->priv, label_);
#endif
}

// Invisible button that allows custom using drawing, but still acts like a button.
FL_INLINE bool fl_button_invisible(const char* label, FlVec2 size, FlButtonFlags flags) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_invisible_impl(g_flowi_button_api->priv, label_, size, flags);
#else
    return (g_flowi_button_api->invisible)(g_flowi_button_api->priv, label_, size, flags);
#endif
}

// Button with a check box state
FL_INLINE bool fl_button_check_box(const char* label, bool* state) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_check_box_impl(g_flowi_button_api->priv, label_, state);
#else
    return (g_flowi_button_api->check_box)(g_flowi_button_api->priv, label_, state);
#endif
}

// Radio button
FL_INLINE bool fl_button_radio(const char* label, bool state) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_radio_impl(g_flowi_button_api->priv, label_, state);
#else
    return (g_flowi_button_api->radio)(g_flowi_button_api->priv, label_, state);
#endif
}

// TODO: Document
FL_INLINE void fl_button_bullet() {
#ifdef FLOWI_STATIC
    fl_button_bullet_impl(g_flowi_button_api->priv);
#else
    (g_flowi_button_api->bullet)(g_flowi_button_api->priv);
#endif
}

// TODO: Document
FL_INLINE bool fl_button_image_with_text(FlImage image, const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_button_image_with_text_impl(g_flowi_button_api->priv, image, label_);
#else
    return (g_flowi_button_api->image_with_text)(g_flowi_button_api->priv, image, label_);
#endif
}
