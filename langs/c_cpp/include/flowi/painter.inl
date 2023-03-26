typedef struct FlPainterApi {
    struct FlInternalData* priv;
    void (*set_layer)(struct FlInternalData* priv, FlPainterLayer layer);
    void (*draw_line)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float thickness);
    void (*draw_rect)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float rounding);
    void (*draw_rect_filled)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float rounding);
    void (*draw_rect_filled_gradient)(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor left, FlColor right,
                                      FlColor btm_right, FlColor btm_left);
} FlPainterApi;

extern FlPainterApi* g_flowi_painter_api;

#ifdef FLOWI_STATIC
void fl_painter_set_layer_impl(struct FlInternalData* priv, FlPainterLayer layer);
void fl_painter_draw_line_impl(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float thickness);
void fl_painter_draw_rect_impl(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float rounding);
void fl_painter_draw_rect_filled_impl(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor color, float rounding);
void fl_painter_draw_rect_filled_gradient_impl(struct FlInternalData* priv, FlVec2 p1, FlVec2 p2, FlColor left,
                                               FlColor right, FlColor btm_right, FlColor btm_left);
#endif

// The current layer to draw on. Default is ActiveWindow.
FL_INLINE void fl_painter_set_layer(FlPainterLayer layer) {
#ifdef FLOWI_STATIC
    fl_painter_set_layer_impl(g_flowi_painter_api->priv, layer);
#else
    (g_flowi_painter_api->set_layer)(g_flowi_painter_api->priv, layer);
#endif
}

// Draw a line from `pos` to `end` with the given `color` and `thickness`.
FL_INLINE void fl_painter_draw_line(FlVec2 p1, FlVec2 p2, FlColor color, float thickness) {
#ifdef FLOWI_STATIC
    fl_painter_draw_line_impl(g_flowi_painter_api->priv, p1, p2, color, thickness);
#else
    (g_flowi_painter_api->draw_line)(g_flowi_painter_api->priv, p1, p2, color, thickness);
#endif
}

// Draw a rectangle with the given `color` and `rounding`.
FL_INLINE void fl_painter_draw_rect(FlVec2 p1, FlVec2 p2, FlColor color, float rounding) {
#ifdef FLOWI_STATIC
    fl_painter_draw_rect_impl(g_flowi_painter_api->priv, p1, p2, color, rounding);
#else
    (g_flowi_painter_api->draw_rect)(g_flowi_painter_api->priv, p1, p2, color, rounding);
#endif
}

// Draw a filled rectangle with the given `color` and `rounding`.
FL_INLINE void fl_painter_draw_rect_filled(FlVec2 p1, FlVec2 p2, FlColor color, float rounding) {
#ifdef FLOWI_STATIC
    fl_painter_draw_rect_filled_impl(g_flowi_painter_api->priv, p1, p2, color, rounding);
#else
    (g_flowi_painter_api->draw_rect_filled)(g_flowi_painter_api->priv, p1, p2, color, rounding);
#endif
}

// Draw a rectangle with a gradient
FL_INLINE void fl_painter_draw_rect_filled_gradient(FlVec2 p1, FlVec2 p2, FlColor left, FlColor right,
                                                    FlColor btm_right, FlColor btm_left) {
#ifdef FLOWI_STATIC
    fl_painter_draw_rect_filled_gradient_impl(g_flowi_painter_api->priv, p1, p2, left, right, btm_right, btm_left);
#else
    (g_flowi_painter_api->draw_rect_filled_gradient)(g_flowi_painter_api->priv, p1, p2, left, right, btm_right,
                                                     btm_left);
#endif
}
