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

extern FlCursorApi* g_flowi_cursor_api;

#ifdef FLOWI_STATIC
void fl_cursor_separator_impl(struct FlInternalData* priv);
void fl_cursor_same_line_impl(struct FlInternalData* priv, float offset_from_start_x, float spacing);
void fl_cursor_new_line_impl(struct FlInternalData* priv);
void fl_cursor_spacing_impl(struct FlInternalData* priv);
void fl_cursor_dummy_impl(struct FlInternalData* priv, FlVec2 size);
void fl_cursor_indent_impl(struct FlInternalData* priv, float indent);
void fl_cursor_unindent_impl(struct FlInternalData* priv, float indent_w);
void fl_cursor_begin_group_impl(struct FlInternalData* priv);
void fl_cursor_end_group_impl(struct FlInternalData* priv);
FlVec2 fl_cursor_get_pos_impl(struct FlInternalData* priv);
float fl_cursor_get_pos_x_impl(struct FlInternalData* priv);
float fl_cursor_get_pos_y_impl(struct FlInternalData* priv);
void fl_cursor_set_pos_impl(struct FlInternalData* priv, FlVec2 pos);
void fl_cursor_set_pos_x_impl(struct FlInternalData* priv, float x);
void fl_cursor_set_pos_y_impl(struct FlInternalData* priv, float y);
FlVec2 fl_cursor_screen_pos_impl(struct FlInternalData* priv);
void fl_cursor_set_screen_pos_impl(struct FlInternalData* priv, FlVec2 pos);
void fl_cursor_align_text_to_frame_padding_impl(struct FlInternalData* priv);
float fl_cursor_get_text_line_height_impl(struct FlInternalData* priv);
float fl_cursor_get_text_line_height_with_spacing_impl(struct FlInternalData* priv);
float fl_cursor_get_frame_height_impl(struct FlInternalData* priv);
float fl_cursor_get_frame_height_with_spacing_impl(struct FlInternalData* priv);
#endif

// Separator, generally horizontal. Inside a menu bar or in horizontal layout mode, this becomes a vertical separator.
FL_INLINE void fl_cursor_separator() {
#ifdef FLOWI_STATIC
    fl_cursor_separator_impl(g_flowi_cursor_api->priv);
#else
    (g_flowi_cursor_api->separator)(g_flowi_cursor_api->priv);
#endif
}

// Call between widgets or groups to layout them horizontally. X position given in window coordinates.
FL_INLINE void fl_cursor_same_line(float offset_from_start_x, float spacing) {
#ifdef FLOWI_STATIC
    fl_cursor_same_line_impl(g_flowi_cursor_api->priv, offset_from_start_x, spacing);
#else
    (g_flowi_cursor_api->same_line)(g_flowi_cursor_api->priv, offset_from_start_x, spacing);
#endif
}

// Undo a same_line() or force a new line when in a horizontal-layout context.
FL_INLINE void fl_cursor_new_line() {
#ifdef FLOWI_STATIC
    fl_cursor_new_line_impl(g_flowi_cursor_api->priv);
#else
    (g_flowi_cursor_api->new_line)(g_flowi_cursor_api->priv);
#endif
}

// Undo a same_line() or force a new line when in a horizontal-layout context.
FL_INLINE void fl_cursor_spacing() {
#ifdef FLOWI_STATIC
    fl_cursor_spacing_impl(g_flowi_cursor_api->priv);
#else
    (g_flowi_cursor_api->spacing)(g_flowi_cursor_api->priv);
#endif
}

// Add a dummy item of given size. Unlike widgets.invisible_button(), dummmy() won't take the mouse click or be
// navigable into.
FL_INLINE void fl_cursor_dummy(FlVec2 size) {
#ifdef FLOWI_STATIC
    fl_cursor_dummy_impl(g_flowi_cursor_api->priv, size);
#else
    (g_flowi_cursor_api->dummy)(g_flowi_cursor_api->priv, size);
#endif
}

// Move content position toward the right, by indent_w, or style.IndentSpacing if indent_w <= 0
FL_INLINE void fl_cursor_indent(float indent) {
#ifdef FLOWI_STATIC
    fl_cursor_indent_impl(g_flowi_cursor_api->priv, indent);
#else
    (g_flowi_cursor_api->indent)(g_flowi_cursor_api->priv, indent);
#endif
}

// Move content position back to the left, by indent_w, or style.IndentSpacing if indent_w <= 0
FL_INLINE void fl_cursor_unindent(float indent_w) {
#ifdef FLOWI_STATIC
    fl_cursor_unindent_impl(g_flowi_cursor_api->priv, indent_w);
#else
    (g_flowi_cursor_api->unindent)(g_flowi_cursor_api->priv, indent_w);
#endif
}

FL_INLINE void fl_cursor_begin_group() {
#ifdef FLOWI_STATIC
    fl_cursor_begin_group_impl(g_flowi_cursor_api->priv);
#else
    (g_flowi_cursor_api->begin_group)(g_flowi_cursor_api->priv);
#endif
}

FL_INLINE void fl_cursor_end_group() {
#ifdef FLOWI_STATIC
    fl_cursor_end_group_impl(g_flowi_cursor_api->priv);
#else
    (g_flowi_cursor_api->end_group)(g_flowi_cursor_api->priv);
#endif
}

// Cursor position in window coordinates (relative to window position)
FL_INLINE FlVec2 fl_cursor_get_pos() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_pos_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_pos)(g_flowi_cursor_api->priv);
#endif
}

FL_INLINE float fl_cursor_get_pos_x() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_pos_x_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_pos_x)(g_flowi_cursor_api->priv);
#endif
}

FL_INLINE float fl_cursor_get_pos_y() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_pos_y_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_pos_y)(g_flowi_cursor_api->priv);
#endif
}

// Set position in window coordinates (relative to window position)
FL_INLINE void fl_cursor_set_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC
    fl_cursor_set_pos_impl(g_flowi_cursor_api->priv, pos);
#else
    (g_flowi_cursor_api->set_pos)(g_flowi_cursor_api->priv, pos);
#endif
}

FL_INLINE void fl_cursor_set_pos_x(float x) {
#ifdef FLOWI_STATIC
    fl_cursor_set_pos_x_impl(g_flowi_cursor_api->priv, x);
#else
    (g_flowi_cursor_api->set_pos_x)(g_flowi_cursor_api->priv, x);
#endif
}

FL_INLINE void fl_cursor_set_pos_y(float y) {
#ifdef FLOWI_STATIC
    fl_cursor_set_pos_y_impl(g_flowi_cursor_api->priv, y);
#else
    (g_flowi_cursor_api->set_pos_y)(g_flowi_cursor_api->priv, y);
#endif
}

// cursor position in absolute coordinates (useful to work with ImDrawList API).
// generally top-left == GetMainViewport()->Pos == (0,0) in single viewport mode,
// and bottom-right == GetMainViewport()->Pos+Size == io.DisplaySize in single-viewport mode.
FL_INLINE FlVec2 fl_cursor_screen_pos() {
#ifdef FLOWI_STATIC
    return fl_cursor_screen_pos_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->screen_pos)(g_flowi_cursor_api->priv);
#endif
}

FL_INLINE void fl_cursor_set_screen_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC
    fl_cursor_set_screen_pos_impl(g_flowi_cursor_api->priv, pos);
#else
    (g_flowi_cursor_api->set_screen_pos)(g_flowi_cursor_api->priv, pos);
#endif
}

// vertically align upcoming text baseline to FramePadding.y so that it will align properly to regularly framed items
// (call if you have text on a line before a framed item)
FL_INLINE void fl_cursor_align_text_to_frame_padding() {
#ifdef FLOWI_STATIC
    fl_cursor_align_text_to_frame_padding_impl(g_flowi_cursor_api->priv);
#else
    (g_flowi_cursor_api->align_text_to_frame_padding)(g_flowi_cursor_api->priv);
#endif
}

// ~ FontSize
FL_INLINE float fl_cursor_get_text_line_height() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_text_line_height_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_text_line_height)(g_flowi_cursor_api->priv);
#endif
}

// ~ FontSize + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of text)
FL_INLINE float fl_cursor_get_text_line_height_with_spacing() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_text_line_height_with_spacing_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_text_line_height_with_spacing)(g_flowi_cursor_api->priv);
#endif
}

// ~ FontSize + style.FramePadding.y * 2
FL_INLINE float fl_cursor_get_frame_height() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_frame_height_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_frame_height)(g_flowi_cursor_api->priv);
#endif
}

// ~ FontSize + style.FramePadding.y * 2 + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of framed
// widgets)
FL_INLINE float fl_cursor_get_frame_height_with_spacing() {
#ifdef FLOWI_STATIC
    return fl_cursor_get_frame_height_with_spacing_impl(g_flowi_cursor_api->priv);
#else
    return (g_flowi_cursor_api->get_frame_height_with_spacing)(g_flowi_cursor_api->priv);
#endif
}
