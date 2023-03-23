typedef struct FlPainterApi {
    struct FlInternalData* priv;
    void (*set_layer)(struct FlInternalData* priv, FlPainterLayer layer);
    void (*draw_line)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float thickness);
    void (*draw_rect)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float rounding);
    void (*draw_rect_filled)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float rounding);
    void (*draw_rect_filled_gradient)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor left, FlColor right,
                                      FlColor btm_right, FlColor btm_left);
} FlPainterApi;

// The current layer to draw on. Default is ActiveWindow.
FL_INLINE void fl_painter_set_layer(FlPainterLayer layer) {
#ifdef FLOWI_STATIC

    fl_painter_set_layer_impl(void* ctx, layer);
#else
    (api->set_layer)(void* ctx, layer);
#endif
}

// Draw a line from `pos` to `end` with the given `color` and `thickness`.
FL_INLINE void fl_painter_draw_line(FlVec2 p1, FlVec2 p2, FlColor color, float thickness) {
#ifdef FLOWI_STATIC

    fl_painter_draw_line_impl(void* ctx, p1, p2, color, thickness);
#else
    (api->draw_line)(void* ctx, p1, p2, color, thickness);
#endif
}

// Draw a rectangle with the given `color` and `rounding`.
FL_INLINE void fl_painter_draw_rect(FlVec2 p1, FlVec2 p2, FlColor color, float rounding) {
#ifdef FLOWI_STATIC

    fl_painter_draw_rect_impl(void* ctx, p1, p2, color, rounding);
#else
    (api->draw_rect)(void* ctx, p1, p2, color, rounding);
#endif
}

// Draw a filled rectangle with the given `color` and `rounding`.
FL_INLINE void fl_painter_draw_rect_filled(FlVec2 p1, FlVec2 p2, FlColor color, float rounding) {
#ifdef FLOWI_STATIC

    fl_painter_draw_rect_filled_impl(void* ctx, p1, p2, color, rounding);
#else
    (api->draw_rect_filled)(void* ctx, p1, p2, color, rounding);
#endif
}

// Draw a rectangle with a gradient
FL_INLINE void fl_painter_draw_rect_filled_gradient(FlVec2 p1, FlVec2 p2, FlColor left, FlColor right,
                                                    FlColor btm_right, FlColor btm_left) {
#ifdef FLOWI_STATIC

    fl_painter_draw_rect_filled_gradient_impl(void* ctx, p1, p2, left, right, btm_right, btm_left);
#else
    (api->draw_rect_filled_gradient)(void* ctx, p1, p2, left, right, btm_right, btm_left);
#endif
}
