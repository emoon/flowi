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

// Show a regular push button
FL_INLINE bool fl_button_regular(struct FlButtonApi* api, const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->regular)(api->priv, label_);
}

// Show a regular push button with a specific size
FL_INLINE bool fl_button_regular_size(struct FlButtonApi* api, const char* label, FlVec2 size) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->regular_size)(api->priv, label_, size);
}

// Show a regular push button without any frame padding.
FL_INLINE bool fl_button_small(struct FlButtonApi* api, const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->small)(api->priv, label_);
}

// Invisible button that allows custom using drawing, but still acts like a button.
FL_INLINE bool fl_button_invisible(struct FlButtonApi* api, const char* label, FlVec2 size, FlButtonFlags flags) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->invisible)(api->priv, label_, size, flags);
}

// Button with a check box state
FL_INLINE bool fl_button_check_box(struct FlButtonApi* api, const char* label, bool* state) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->check_box)(api->priv, label_, state);
}

// Radio button
FL_INLINE bool fl_button_radio(struct FlButtonApi* api, const char* label, bool state) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->radio)(api->priv, label_, state);
}

// TODO: Document
FL_INLINE void fl_button_bullet(struct FlButtonApi* api) {
    (api->bullet)(api->priv);
}

// TODO: Document
FL_INLINE bool fl_button_image_with_text(struct FlButtonApi* api, FlImage image, const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->image_with_text)(api->priv, image, label_);
}
