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
FL_INLINE void fl_painter_set_layer(struct FlPainterApi* api, FlPainterLayer layer) {
    (api->set_layer)(api->priv, layer);
}

// Draw a line from `pos` to `end` with the given `color` and `thickness`.
FL_INLINE void fl_painter_draw_line(struct FlPainterApi* api, FlVec2 p1, FlVec2 p2, FlColor color, float thickness) {
    (api->draw_line)(api->priv, p1, p2, color, thickness);
}

// Draw a rectangle with the given `color` and `rounding`.
FL_INLINE void fl_painter_draw_rect(struct FlPainterApi* api, FlVec2 p1, FlVec2 p2, FlColor color, float rounding) {
    (api->draw_rect)(api->priv, p1, p2, color, rounding);
}

// Draw a filled rectangle with the given `color` and `rounding`.
FL_INLINE void fl_painter_draw_rect_filled(struct FlPainterApi* api, FlVec2 p1, FlVec2 p2, FlColor color,
                                           float rounding) {
    (api->draw_rect_filled)(api->priv, p1, p2, color, rounding);
}

// Draw a rectangle with a gradient
FL_INLINE void fl_painter_draw_rect_filled_gradient(struct FlPainterApi* api, FlVec2 p1, FlVec2 p2, FlColor left,
                                                    FlColor right, FlColor btm_right, FlColor btm_left) {
    (api->draw_rect_filled_gradient)(api->priv, p1, p2, left, right, btm_right, btm_left);
}
