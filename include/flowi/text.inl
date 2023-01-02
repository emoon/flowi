typedef struct FlTextApi {
    struct FlInternalData* priv;
    void (*bullet)(struct FlInternalData* priv, FlString text);
    void (*label)(struct FlInternalData* priv, FlString label, FlString text);
    void (*show_color)(struct FlInternalData* priv, FlColor color, FlString text);
    void (*show)(struct FlInternalData* priv, FlString text);
    void (*text_disabled)(struct FlInternalData* priv, FlString text);
} FlTextApi;

FL_INLINE void fl_text_bullet(struct FlTextApi* api, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    (api->bullet)(api->priv, text_);
}

FL_INLINE void fl_text_label(struct FlTextApi* api, const char* label, const char* text) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString text_ = fl_cstr_to_flstring(text);
    (api->label)(api->priv, label_, text_);
}

FL_INLINE void fl_text_show_color(struct FlTextApi* api, FlColor color, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    (api->show_color)(api->priv, color, text_);
}

FL_INLINE void fl_text_show(struct FlTextApi* api, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    (api->show)(api->priv, text_);
}

FL_INLINE void fl_text_text_disabled(struct FlTextApi* api, const char* text) {
    FlString text_ = fl_cstr_to_flstring(text);
    (api->text_disabled)(api->priv, text_);
}
