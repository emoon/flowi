typedef struct FlTextApi {
    struct FlInternalData* priv;
    FlVec2 (*calc_size)(struct FlInternalData* priv, FlString text);
    void (*bullet)(struct FlInternalData* priv, FlString text);
    void (*label)(struct FlInternalData* priv, FlString label, FlString text);
    void (*show_color)(struct FlInternalData* priv, FlColor color, FlString text);
    void (*show)(struct FlInternalData* priv, FlString text);
    void (*text_disabled)(struct FlInternalData* priv, FlString text);
} FlTextApi;

extern FlTextApi* g_flowi_text_api;

#ifdef FLOWI_STATIC
FlVec2 fl_text_calc_size_impl(struct FlInternalData* priv, FlString text);
void fl_text_bullet_impl(struct FlInternalData* priv, FlString text);
void fl_text_label_impl(struct FlInternalData* priv, FlString label, FlString text);
void fl_text_show_color_impl(struct FlInternalData* priv, FlColor color, FlString text);
void fl_text_show_impl(struct FlInternalData* priv, FlString text);
void fl_text_text_disabled_impl(struct FlInternalData* priv, FlString text);
#endif

// Calculate the size of a text string in pixels
FL_INLINE FlVec2 fl_text_calc_size(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    return fl_text_calc_size_impl(g_flowi_text_api->priv, text_);
#else
    return (g_flowi_text_api->calc_size)(g_flowi_text_api->priv, text_);
#endif
}

// Bullet text
FL_INLINE void fl_text_bullet(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    fl_text_bullet_impl(g_flowi_text_api->priv, text_);
#else
    (g_flowi_text_api->bullet)(g_flowi_text_api->priv, text_);
#endif
}

// Draw basic text
FL_INLINE void fl_text_label(const char* label, const char* text) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    fl_text_label_impl(g_flowi_text_api->priv, label_, text_);
#else
    (g_flowi_text_api->label)(g_flowi_text_api->priv, label_, text_);
#endif
}

// Draw basic text with a color
FL_INLINE void fl_text_show_color(FlColor color, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    fl_text_show_color_impl(g_flowi_text_api->priv, color, text_);
#else
    (g_flowi_text_api->show_color)(g_flowi_text_api->priv, color, text_);
#endif
}

// Show basic text
FL_INLINE void fl_text_show(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    fl_text_show_impl(g_flowi_text_api->priv, text_);
#else
    (g_flowi_text_api->show)(g_flowi_text_api->priv, text_);
#endif
}

// Draw text disabled
FL_INLINE void fl_text_text_disabled(const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
#ifdef FLOWI_STATIC
    fl_text_text_disabled_impl(g_flowi_text_api->priv, text_);
#else
    (g_flowi_text_api->text_disabled)(g_flowi_text_api->priv, text_);
#endif
}
