typedef struct FlTextApi {
    struct FlInternalData* priv;
    FlVec2 (*calc_size)(struct FlInternalData* priv, FlString text);
    void (*bullet)(struct FlInternalData* priv, FlString text);
    void (*label)(struct FlInternalData* priv, FlString label, FlString text);
    void (*show_color)(struct FlInternalData* priv, FlColor color, FlString text);
    void (*show)(struct FlInternalData* priv, FlString text);
    void (*text_disabled)(struct FlInternalData* priv, FlString text);
} FlTextApi;

// Calculate the size of a text string in pixels
FL_INLINE FlVec2 fl_text_calc_size(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC

        return fl_text_calc_size_impl(void* ctx, text_);
#else
    return (api->calc_size)(void* ctx, text_);
#endif
}

// Bullet text
FL_INLINE void fl_text_bullet(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC

    fl_text_bullet_impl(void* ctx, text_);
#else
    (api->bullet)(void* ctx, text_);
#endif
}

// Draw basic text
FL_INLINE void fl_text_label(const char* label, const char* text) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC

    fl_text_label_impl(void* ctx, label_, text_);
#else
    (api->label)(void* ctx, label_, text_);
#endif
}

// Draw basic text with a color
FL_INLINE void fl_text_show_color(FlColor color, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC

    fl_text_show_color_impl(void* ctx, color, text_);
#else
    (api->show_color)(void* ctx, color, text_);
#endif
}

// Show basic text
FL_INLINE void fl_text_show(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC

    fl_text_show_impl(void* ctx, text_);
#else
    (api->show)(void* ctx, text_);
#endif
}

// Draw text disabled
FL_INLINE void fl_text_text_disabled(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC

    fl_text_text_disabled_impl(void* ctx, text_);
#else
    (api->text_disabled)(void* ctx, text_);
#endif
}
