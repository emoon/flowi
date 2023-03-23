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
FL_INLINE void fl_style_set_color(FlStyleColor color, FlColor value) {
#ifdef FLOWI_STATIC

    fl_style_set_color_impl(void* ctx, color, value);
#else
    (api->set_color)(void* ctx, color, value);
#endif
}

// Permantly set a color (ARGB)
FL_INLINE void fl_style_set_color_u32(FlStyleColor color, uint32_t value) {
#ifdef FLOWI_STATIC

    fl_style_set_color_u32_impl(void* ctx, color, value);
#else
    (api->set_color_u32)(void* ctx, color, value);
#endif
}

// Temporary push a color change (ARGB)
FL_INLINE void fl_style_push_color_u32(FlStyleColor color, uint32_t value) {
#ifdef FLOWI_STATIC

    fl_style_push_color_u32_impl(void* ctx, color, value);
#else
    (api->push_color_u32)(void* ctx, color, value);
#endif
}

// Temporary push a color change
FL_INLINE void fl_style_push_color(FlStyleColor color, FlColor value) {
#ifdef FLOWI_STATIC

    fl_style_push_color_impl(void* ctx, color, value);
#else
    (api->push_color)(void* ctx, color, value);
#endif
}

// Temporary push a color change
FL_INLINE void fl_style_pop_color() {
#ifdef FLOWI_STATIC

    fl_style_pop_color_impl(void* ctx);
#else
    (api->pop_color)(void* ctx);
#endif
}

// Pushes a single style change
FL_INLINE void fl_style_push_single(FlStyleSingle style, float value) {
#ifdef FLOWI_STATIC

    fl_style_push_single_impl(void* ctx, style, value);
#else
    (api->push_single)(void* ctx, style, value);
#endif
}

// Pushes a Vec2 style change
FL_INLINE void fl_style_push_vec2(FlStyleVec2 style, FlVec2 value) {
#ifdef FLOWI_STATIC

    fl_style_push_vec2_impl(void* ctx, style, value);
#else
    (api->push_vec2)(void* ctx, style, value);
#endif
}

// Pops a style change
FL_INLINE void fl_style_pop() {
#ifdef FLOWI_STATIC

    fl_style_pop_impl(void* ctx);
#else
    (api->pop)(void* ctx);
#endif
}
