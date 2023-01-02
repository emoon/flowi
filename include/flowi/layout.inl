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
FL_INLINE void fl_cursor_separator(struct FlCursorApi* api) {
    (api->separator)(api->priv);
}

// Call between widgets or groups to layout them horizontally. X position given in window coordinates.
FL_INLINE void fl_cursor_same_line(struct FlCursorApi* api, float offset_from_start_x, float spacing) {
    (api->same_line)(api->priv, offset_from_start_x, spacing);
}

// Undo a same_line() or force a new line when in a horizontal-layout context.
FL_INLINE void fl_cursor_new_line(struct FlCursorApi* api) {
    (api->new_line)(api->priv);
}

// Undo a same_line() or force a new line when in a horizontal-layout context.
FL_INLINE void fl_cursor_spacing(struct FlCursorApi* api) {
    (api->spacing)(api->priv);
}

// Add a dummy item of given size. Unlike widgets.invisible_button(), dummmy() won't take the mouse click or be
// navigable into.
FL_INLINE void fl_cursor_dummy(struct FlCursorApi* api, FlVec2 size) {
    (api->dummy)(api->priv, size);
}

// Move content position toward the right, by indent_w, or style.IndentSpacing if indent_w <= 0
FL_INLINE void fl_cursor_indent(struct FlCursorApi* api, float indent) {
    (api->indent)(api->priv, indent);
}

// Move content position back to the left, by indent_w, or style.IndentSpacing if indent_w <= 0
FL_INLINE void fl_cursor_unindent(struct FlCursorApi* api, float indent_w) {
    (api->unindent)(api->priv, indent_w);
}

FL_INLINE void fl_cursor_begin_group(struct FlCursorApi* api) {
    (api->begin_group)(api->priv);
}

FL_INLINE void fl_cursor_end_group(struct FlCursorApi* api) {
    (api->end_group)(api->priv);
}

// Cursor position in window coordinates (relative to window position)
FL_INLINE FlVec2 fl_cursor_get_pos(struct FlCursorApi* api) {
    return (api->get_pos)(api->priv);
}

FL_INLINE float fl_cursor_get_pos_x(struct FlCursorApi* api) {
    return (api->get_pos_x)(api->priv);
}

FL_INLINE float fl_cursor_get_pos_y(struct FlCursorApi* api) {
    return (api->get_pos_y)(api->priv);
}

// Set position in window coordinates (relative to window position)
FL_INLINE void fl_cursor_set_pos(struct FlCursorApi* api, FlVec2 pos) {
    (api->set_pos)(api->priv, pos);
}

FL_INLINE void fl_cursor_set_pos_x(struct FlCursorApi* api, float x) {
    (api->set_pos_x)(api->priv, x);
}

FL_INLINE void fl_cursor_set_pos_y(struct FlCursorApi* api, float y) {
    (api->set_pos_y)(api->priv, y);
}

// cursor position in absolute coordinates (useful to work with ImDrawList API).
// generally top-left == GetMainViewport()->Pos == (0,0) in single viewport mode,
// and bottom-right == GetMainViewport()->Pos+Size == io.DisplaySize in single-viewport mode.
FL_INLINE FlVec2 fl_cursor_screen_pos(struct FlCursorApi* api) {
    return (api->screen_pos)(api->priv);
}

FL_INLINE void fl_cursor_set_screen_pos(struct FlCursorApi* api, FlVec2 pos) {
    (api->set_screen_pos)(api->priv, pos);
}

// vertically align upcoming text baseline to FramePadding.y so that it will align properly to regularly framed items
// (call if you have text on a line before a framed item)
FL_INLINE void fl_cursor_align_text_to_frame_padding(struct FlCursorApi* api) {
    (api->align_text_to_frame_padding)(api->priv);
}

// ~ FontSize
FL_INLINE float fl_cursor_get_text_line_height(struct FlCursorApi* api) {
    return (api->get_text_line_height)(api->priv);
}

// ~ FontSize + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of text)
FL_INLINE float fl_cursor_get_text_line_height_with_spacing(struct FlCursorApi* api) {
    return (api->get_text_line_height_with_spacing)(api->priv);
}

// ~ FontSize + style.FramePadding.y * 2
FL_INLINE float fl_cursor_get_frame_height(struct FlCursorApi* api) {
    return (api->get_frame_height)(api->priv);
}

// ~ FontSize + style.FramePadding.y * 2 + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of framed
// widgets)
FL_INLINE float fl_cursor_get_frame_height_with_spacing(struct FlCursorApi* api) {
    return (api->get_frame_height_with_spacing)(api->priv);
}
