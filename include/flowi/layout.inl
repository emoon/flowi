FlLayoutAreaId fl_layout_area_create_impl(struct FlContext* ctx, FlString name, FlLayoutArea area);

FL_INLINE FlLayoutAreaId fl_layout_area_create(struct FlContext* ctx, const char* name, FlLayoutArea area) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_layout_area_create_impl(ctx, name_, area);
}

FlLayoutAreaId fl_layout_area_from_children_impl(struct FlContext* ctx, FlString name, FlLayoutArea* children,
                                                 uint32_t children_size, int16_t row, int16_t cols);

FL_INLINE FlLayoutAreaId fl_layout_area_from_children(struct FlContext* ctx, const char* name, FlLayoutArea* children,
                                                      uint32_t children_size, int16_t row, int16_t cols) {
    FlString name_ = fl_cstr_to_flstring(name);
    return fl_layout_area_from_children_impl(ctx, name_, children, children_size, row, cols);
}

void fl_layout_area_set_layout_mode_impl(struct FlContext* ctx, FlLayoutMode mode);

FL_INLINE void fl_layout_area_set_layout_mode(struct FlContext* ctx, FlLayoutMode mode) {
    fl_layout_area_set_layout_mode_impl(ctx, mode);
}
