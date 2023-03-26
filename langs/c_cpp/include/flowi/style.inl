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

extern FlStyleApi* g_flowi_style_api;

#ifdef FLOWI_STATIC
void fl_style_set_color_impl(struct FlInternalData* priv, FlStyleColor color, FlColor value);
void fl_style_set_color_u32_impl(struct FlInternalData* priv, FlStyleColor color, uint32_t value);
void fl_style_push_color_u32_impl(struct FlInternalData* priv, FlStyleColor color, uint32_t value);
void fl_style_push_color_impl(struct FlInternalData* priv, FlStyleColor color, FlColor value);
void fl_style_pop_color_impl(struct FlInternalData* priv);
void fl_style_push_single_impl(struct FlInternalData* priv, FlStyleSingle style, float value);
void fl_style_push_vec2_impl(struct FlInternalData* priv, FlStyleVec2 style, FlVec2 value);
void fl_style_pop_impl(struct FlInternalData* priv);
#endif

// Permantly set a color
FL_INLINE void fl_style_set_color(FlStyleColor color, FlColor value) {
#ifdef FLOWI_STATIC
    fl_style_set_color_impl(g_flowi_style_api->priv, color, value);
#else
    (g_flowi_style_api->set_color)(g_flowi_style_api->priv, color, value);
#endif
}

// Permantly set a color (ARGB)
FL_INLINE void fl_style_set_color_u32(FlStyleColor color, uint32_t value) {
#ifdef FLOWI_STATIC
    fl_style_set_color_u32_impl(g_flowi_style_api->priv, color, value);
#else
    (g_flowi_style_api->set_color_u32)(g_flowi_style_api->priv, color, value);
#endif
}

// Temporary push a color change (ARGB)
FL_INLINE void fl_style_push_color_u32(FlStyleColor color, uint32_t value) {
#ifdef FLOWI_STATIC
    fl_style_push_color_u32_impl(g_flowi_style_api->priv, color, value);
#else
    (g_flowi_style_api->push_color_u32)(g_flowi_style_api->priv, color, value);
#endif
}

// Temporary push a color change
FL_INLINE void fl_style_push_color(FlStyleColor color, FlColor value) {
#ifdef FLOWI_STATIC
    fl_style_push_color_impl(g_flowi_style_api->priv, color, value);
#else
    (g_flowi_style_api->push_color)(g_flowi_style_api->priv, color, value);
#endif
}

// Temporary push a color change
FL_INLINE void fl_style_pop_color() {
#ifdef FLOWI_STATIC
    fl_style_pop_color_impl(g_flowi_style_api->priv);
#else
    (g_flowi_style_api->pop_color)(g_flowi_style_api->priv);
#endif
}

// Pushes a single style change
FL_INLINE void fl_style_push_single(FlStyleSingle style, float value) {
#ifdef FLOWI_STATIC
    fl_style_push_single_impl(g_flowi_style_api->priv, style, value);
#else
    (g_flowi_style_api->push_single)(g_flowi_style_api->priv, style, value);
#endif
}

// Pushes a Vec2 style change
FL_INLINE void fl_style_push_vec2(FlStyleVec2 style, FlVec2 value) {
#ifdef FLOWI_STATIC
    fl_style_push_vec2_impl(g_flowi_style_api->priv, style, value);
#else
    (g_flowi_style_api->push_vec2)(g_flowi_style_api->priv, style, value);
#endif
}

// Pops a style change
FL_INLINE void fl_style_pop() {
#ifdef FLOWI_STATIC
    fl_style_pop_impl(g_flowi_style_api->priv);
#else
    (g_flowi_style_api->pop)(g_flowi_style_api->priv);
#endif
}
