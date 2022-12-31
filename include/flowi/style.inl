typedef struct FlStyleApi {
    struct FlInternalData* priv;
    void (*set_color)(struct FlInternalData* priv, FlStyleColor color, FlColor value);
    void (*set_color_u32)(struct FlInternalData* priv, FlStyleColor color, uint32_t value);
    void (*push_color_u32)(struct FlInternalData* priv, FlStyleColor color, uint32_t value);
    void (*push_color)(struct FlInternalData* priv, FlStyleColor color, FlColor value);
    void (*pop_color)(struct FlInternalData* priv);
    void (*push_single)(struct FlInternalData* priv, FlStyleSingle style, float value);
    void (*push_vec2)(struct FlInternalData* priv, FlStyleVec2 style, FlVec2 value);
    void (*pop)(struct FlInternalData* priv);
} FlStyleApi;

// Permantly set a color
FL_INLINE void fl_style_set_color(struct FlStyleApi* api, FlStyleColor color, FlColor value) {
    (api->set_color)(api->priv, color, value);
}

// Permantly set a color (ARGB)
FL_INLINE void fl_style_set_color_u32(struct FlStyleApi* api, FlStyleColor color, uint32_t value) {
    (api->set_color_u32)(api->priv, color, value);
}

// Temporary push a color change (ARGB)
FL_INLINE void fl_style_push_color_u32(struct FlStyleApi* api, FlStyleColor color, uint32_t value) {
    (api->push_color_u32)(api->priv, color, value);
}

// Temporary push a color change
FL_INLINE void fl_style_push_color(struct FlStyleApi* api, FlStyleColor color, FlColor value) {
    (api->push_color)(api->priv, color, value);
}

// Temporary push a color change
FL_INLINE void fl_style_pop_color(struct FlStyleApi* api) {
    (api->pop_color)(api->priv);
}

// Pushes a single style change
FL_INLINE void fl_style_push_single(struct FlStyleApi* api, FlStyleSingle style, float value) {
    (api->push_single)(api->priv, style, value);
}

// Pushes a Vec2 style change
FL_INLINE void fl_style_push_vec2(struct FlStyleApi* api, FlStyleVec2 style, FlVec2 value) {
    (api->push_vec2)(api->priv, style, value);
}

// Pops a style change
FL_INLINE void fl_style_pop(struct FlStyleApi* api) {
    (api->pop)(api->priv);
}
