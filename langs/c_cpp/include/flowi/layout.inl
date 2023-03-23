typedef struct FlCursorApi {
    struct FlInternalData* priv;
    void (*separator)(struct FlInternalData* priv);
    void (*same_line)(struct FlInternalData* priv, float offset_from_start_x, float spacing);
    void (*new_line)(struct FlInternalData* priv);
    void (*spacing)(struct FlInternalData* priv);
    void (*dummy)(struct FlInternalData* priv, FlVec2 size);
    void (*indent)(struct FlInternalData* priv, float indent);
    void (*unindent)(struct FlInternalData* priv, float indent_w);
    void (*begin_group)(struct FlInternalData* priv);
    void (*end_group)(struct FlInternalData* priv);
    FlVec2 (*get_pos)(struct FlInternalData* priv);
    float (*get_pos_x)(struct FlInternalData* priv);
    float (*get_pos_y)(struct FlInternalData* priv);
    void (*set_pos)(struct FlInternalData* priv, FlVec2 pos);
    void (*set_pos_x)(struct FlInternalData* priv, float x);
    void (*set_pos_y)(struct FlInternalData* priv, float y);
    FlVec2 (*screen_pos)(struct FlInternalData* priv);
    void (*set_screen_pos)(struct FlInternalData* priv, FlVec2 pos);
    void (*align_text_to_frame_padding)(struct FlInternalData* priv);
    float (*get_text_line_height)(struct FlInternalData* priv);
    float (*get_text_line_height_with_spacing)(struct FlInternalData* priv);
    float (*get_frame_height)(struct FlInternalData* priv);
    float (*get_frame_height_with_spacing)(struct FlInternalData* priv);
} FlCursorApi;

// Separator, generally horizontal. Inside a menu bar or in horizontal layout mode, this becomes a vertical separator.
FL_INLINE void fl_cursor_separator() {
#ifdef FLOWI_STATIC

    fl_cursor_separator_impl(void* ctx);
#else
    (api->separator)(void* ctx);
#endif
}

// Call between widgets or groups to layout them horizontally. X position given in window coordinates.
FL_INLINE void fl_cursor_same_line(float offset_from_start_x, float spacing) {
#ifdef FLOWI_STATIC

    fl_cursor_same_line_impl(void* ctx, offset_from_start_x, spacing);
#else
    (api->same_line)(void* ctx, offset_from_start_x, spacing);
#endif
}

// Undo a same_line() or force a new line when in a horizontal-layout context.
FL_INLINE void fl_cursor_new_line() {
#ifdef FLOWI_STATIC

    fl_cursor_new_line_impl(void* ctx);
#else
    (api->new_line)(void* ctx);
#endif
}

// Undo a same_line() or force a new line when in a horizontal-layout context.
FL_INLINE void fl_cursor_spacing() {
#ifdef FLOWI_STATIC

    fl_cursor_spacing_impl(void* ctx);
#else
    (api->spacing)(void* ctx);
#endif
}

// Add a dummy item of given size. Unlike widgets.invisible_button(), dummmy() won't take the mouse click or be
// navigable into.
FL_INLINE void fl_cursor_dummy(FlVec2 size) {
#ifdef FLOWI_STATIC

    fl_cursor_dummy_impl(void* ctx, size);
#else
    (api->dummy)(void* ctx, size);
#endif
}

// Move content position toward the right, by indent_w, or style.IndentSpacing if indent_w <= 0
FL_INLINE void fl_cursor_indent(float indent) {
#ifdef FLOWI_STATIC

    fl_cursor_indent_impl(void* ctx, indent);
#else
    (api->indent)(void* ctx, indent);
#endif
}

// Move content position back to the left, by indent_w, or style.IndentSpacing if indent_w <= 0
FL_INLINE void fl_cursor_unindent(float indent_w) {
#ifdef FLOWI_STATIC

    fl_cursor_unindent_impl(void* ctx, indent_w);
#else
    (api->unindent)(void* ctx, indent_w);
#endif
}

FL_INLINE void fl_cursor_begin_group() {
#ifdef FLOWI_STATIC

    fl_cursor_begin_group_impl(void* ctx);
#else
    (api->begin_group)(void* ctx);
#endif
}

FL_INLINE void fl_cursor_end_group() {
#ifdef FLOWI_STATIC

    fl_cursor_end_group_impl(void* ctx);
#else
    (api->end_group)(void* ctx);
#endif
}

// Cursor position in window coordinates (relative to window position)
FL_INLINE FlVec2 fl_cursor_get_pos() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_pos_impl(void* ctx);
#else
    return (api->get_pos)(void* ctx);
#endif
}

FL_INLINE float fl_cursor_get_pos_x() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_pos_x_impl(void* ctx);
#else
    return (api->get_pos_x)(void* ctx);
#endif
}

FL_INLINE float fl_cursor_get_pos_y() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_pos_y_impl(void* ctx);
#else
    return (api->get_pos_y)(void* ctx);
#endif
}

// Set position in window coordinates (relative to window position)
FL_INLINE void fl_cursor_set_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC

    fl_cursor_set_pos_impl(void* ctx, pos);
#else
    (api->set_pos)(void* ctx, pos);
#endif
}

FL_INLINE void fl_cursor_set_pos_x(float x) {
#ifdef FLOWI_STATIC

    fl_cursor_set_pos_x_impl(void* ctx, x);
#else
    (api->set_pos_x)(void* ctx, x);
#endif
}

FL_INLINE void fl_cursor_set_pos_y(float y) {
#ifdef FLOWI_STATIC

    fl_cursor_set_pos_y_impl(void* ctx, y);
#else
    (api->set_pos_y)(void* ctx, y);
#endif
}

// cursor position in absolute coordinates (useful to work with ImDrawList API).
// generally top-left == GetMainViewport()->Pos == (0,0) in single viewport mode,
// and bottom-right == GetMainViewport()->Pos+Size == io.DisplaySize in single-viewport mode.
FL_INLINE FlVec2 fl_cursor_screen_pos() {
#ifdef FLOWI_STATIC

    return fl_cursor_screen_pos_impl(void* ctx);
#else
    return (api->screen_pos)(void* ctx);
#endif
}

FL_INLINE void fl_cursor_set_screen_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC

    fl_cursor_set_screen_pos_impl(void* ctx, pos);
#else
    (api->set_screen_pos)(void* ctx, pos);
#endif
}

// vertically align upcoming text baseline to FramePadding.y so that it will align properly to regularly framed items
// (call if you have text on a line before a framed item)
FL_INLINE void fl_cursor_align_text_to_frame_padding() {
#ifdef FLOWI_STATIC

    fl_cursor_align_text_to_frame_padding_impl(void* ctx);
#else
    (api->align_text_to_frame_padding)(void* ctx);
#endif
}

// ~ FontSize
FL_INLINE float fl_cursor_get_text_line_height() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_text_line_height_impl(void* ctx);
#else
    return (api->get_text_line_height)(void* ctx);
#endif
}

// ~ FontSize + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of text)
FL_INLINE float fl_cursor_get_text_line_height_with_spacing() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_text_line_height_with_spacing_impl(void* ctx);
#else
    return (api->get_text_line_height_with_spacing)(void* ctx);
#endif
}

// ~ FontSize + style.FramePadding.y * 2
FL_INLINE float fl_cursor_get_frame_height() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_frame_height_impl(void* ctx);
#else
    return (api->get_frame_height)(void* ctx);
#endif
}

// ~ FontSize + style.FramePadding.y * 2 + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of framed
// widgets)
FL_INLINE float fl_cursor_get_frame_height_with_spacing() {
#ifdef FLOWI_STATIC

    return fl_cursor_get_frame_height_with_spacing_impl(void* ctx);
#else
    return (api->get_frame_height_with_spacing)(void* ctx);
#endif
}
