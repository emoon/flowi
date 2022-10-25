// Permantly set a color
void fl_style_set_color_impl(struct FlContext* ctx, FlStyleColor color, FlColor value);

FL_INLINE void fl_style_set_color(struct FlContext* ctx, FlStyleColor color, FlColor value) {
    fl_style_set_color_impl(ctx, color, value);
}

// Permantly set a color (ARGB)
void fl_style_set_color_u32_impl(struct FlContext* ctx, FlStyleColor color, uint32_t value);

FL_INLINE void fl_style_set_color_u32(struct FlContext* ctx, FlStyleColor color, uint32_t value) {
    fl_style_set_color_u32_impl(ctx, color, value);
}

// Temporary push a color change (ARGB)
void fl_style_push_color_u32_impl(struct FlContext* ctx, FlStyleColor color, uint32_t value);

FL_INLINE void fl_style_push_color_u32(struct FlContext* ctx, FlStyleColor color, uint32_t value) {
    fl_style_push_color_u32_impl(ctx, color, value);
}

// Temporary push a color change
void fl_style_push_color_impl(struct FlContext* ctx, FlStyleColor color, FlColor value);

FL_INLINE void fl_style_push_color(struct FlContext* ctx, FlStyleColor color, FlColor value) {
    fl_style_push_color_impl(ctx, color, value);
}

// Temporary push a color change
void fl_style_pop_color_impl(struct FlContext* ctx);

FL_INLINE void fl_style_pop_color(struct FlContext* ctx) {
    fl_style_pop_color_impl(ctx);
}

// Pushes a single style change
void fl_style_push_single_impl(struct FlContext* ctx, FlStyleSingle style, float value);

FL_INLINE void fl_style_push_single(struct FlContext* ctx, FlStyleSingle style, float value) {
    fl_style_push_single_impl(ctx, style, value);
}

// Pushes a Vec2 style change
void fl_style_push_vec2_impl(struct FlContext* ctx, FlStyleVec2 style, FlVec2 value);

FL_INLINE void fl_style_push_vec2(struct FlContext* ctx, FlStyleVec2 style, FlVec2 value) {
    fl_style_push_vec2_impl(ctx, style, value);
}

// Pops a style change
void fl_style_pop_impl(struct FlContext* ctx);

FL_INLINE void fl_style_pop(struct FlContext* ctx) {
    fl_style_pop_impl(ctx);
}
