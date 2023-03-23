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
FL_INLINE bool fl_button_regular(const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_regular_impl(void* ctx, label_);
#else
    return (api->regular)(void* ctx, label_);
#endif
}

// Show a regular push button with a specific size
FL_INLINE bool fl_button_regular_size(const char* label, FlVec2 size) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_regular_size_impl(void* ctx, label_, size);
#else
    return (api->regular_size)(void* ctx, label_, size);
#endif
}

// Show a regular push button without any frame padding.
FL_INLINE bool fl_button_small(const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_small_impl(void* ctx, label_);
#else
    return (api->small)(void* ctx, label_);
#endif
}

// Invisible button that allows custom using drawing, but still acts like a button.
FL_INLINE bool fl_button_invisible(const char* label, FlVec2 size, FlButtonFlags flags) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_invisible_impl(void* ctx, label_, size, flags);
#else
    return (api->invisible)(void* ctx, label_, size, flags);
#endif
}

// Button with a check box state
FL_INLINE bool fl_button_check_box(const char* label, bool* state) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_check_box_impl(void* ctx, label_, state);
#else
    return (api->check_box)(void* ctx, label_, state);
#endif
}

// Radio button
FL_INLINE bool fl_button_radio(const char* label, bool state) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_radio_impl(void* ctx, label_, state);
#else
    return (api->radio)(void* ctx, label_, state);
#endif
}

// TODO: Document
FL_INLINE void fl_button_bullet() {
#ifdef FLOWI_STATIC

    fl_button_bullet_impl(void* ctx);
#else
    (api->bullet)(void* ctx);
#endif
}

// TODO: Document
FL_INLINE bool fl_button_image_with_text(FlImage image, const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_button_image_with_text_impl(void* ctx, image, label_);
#else
    return (api->image_with_text)(void* ctx, image, label_);
#endif
}
