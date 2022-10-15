// Create a new style
FlStyle* fl_style_create_impl(struct FlContext* ctx, FlString name);

FL_INLINE FlStyle* fl_style_create(struct FlContext* ctx, const char* name) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_style_create_impl(ctx, name_);
}

// Get the default style. Changing this will apply the base style for the whole application
FlStyle* fl_style_get_default_impl(struct FlContext* ctx);

FL_INLINE FlStyle* fl_style_get_default(struct FlContext* ctx) {
    return fl_style_get_default_impl(ctx);
}

// Get the current style which is based on what has been pushed on the style stack using push/pop
FlStyle fl_style_get_current_impl(struct FlContext* ctx);

FL_INLINE FlStyle fl_style_get_current(struct FlContext* ctx) {
    return fl_style_get_current_impl(ctx);
}

// Mark the end of style changes
void fl_style_end_changes_impl(struct FlContext* ctx, FlStyle* style);

FL_INLINE void fl_style_end_changes(struct FlContext* ctx, FlStyle* style) {
    fl_style_end_changes_impl(ctx, style);
}

// Select the style to be used, to end using the style use 'fl_pop_style()'
void fl_style_push_impl(struct FlContext* ctx, FlStyle* style);

FL_INLINE void fl_style_push(struct FlContext* ctx, FlStyle* style) {
    fl_style_push_impl(ctx, style);
}

// Pops the current style
void fl_style_pop_impl(struct FlContext* ctx);

FL_INLINE void fl_style_pop(struct FlContext* ctx) {
    fl_style_pop_impl(ctx);
}
