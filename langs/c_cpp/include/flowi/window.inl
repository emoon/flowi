typedef struct FlWindowApi {
    struct FlInternalData* priv;
    void (*set_pos)(struct FlInternalData* priv, FlVec2 pos);
    bool (*begin)(struct FlInternalData* priv, FlString name, FlWindowFlags flags);
    void (*end)(struct FlInternalData* priv);
    bool (*begin_child)(struct FlInternalData* priv, FlString id, FlVec2 size, bool border, FlWindowFlags flags);
    void (*end_child)(struct FlInternalData* priv);
    bool (*is_appearing)(struct FlInternalData* priv);
    bool (*is_collapsed)(struct FlInternalData* priv);
    bool (*is_focused)(struct FlInternalData* priv, FlFocusedFlags flags);
    bool (*is_hovered)(struct FlInternalData* priv, FlHoveredFlags flags);
    float (*dpi_scale)(struct FlInternalData* priv);
    FlVec2 (*pos)(struct FlInternalData* priv);
    FlVec2 (*size)(struct FlInternalData* priv);
} FlWindowApi;

// Sets the position of the next window, call before begin()
FL_INLINE void fl_window_set_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC

    fl_window_set_pos_impl(void* ctx, pos);
#else
    (api->set_pos)(void* ctx, pos);
#endif
}

// Always call a matching end() for each begin() call, regardless of its return value!
FL_INLINE bool fl_window_begin(const char* name, FlWindowFlags flags) {
    FlString name_ = fl_cstr_to_flstring(name);
#ifdef FLOWI_STATIC

        return fl_window_begin_impl(void* ctx, name_, flags);
#else
    return (api->begin)(void* ctx, name_, flags);
#endif
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_window_end() {
#ifdef FLOWI_STATIC

    fl_window_end_impl(void* ctx);
#else
    (api->end)(void* ctx);
#endif
}

// Call between begin() and end() to create a child window. Child windows can embed their own child.
FL_INLINE bool fl_window_begin_child(const char* id, FlVec2 size, bool border, FlWindowFlags flags) {
    FlString id_ = fl_cstr_to_flstring(id);
#ifdef FLOWI_STATIC

        return fl_window_begin_child_impl(void* ctx, id_, size, border, flags);
#else
    return (api->begin_child)(void* ctx, id_, size, border, flags);
#endif
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_window_end_child() {
#ifdef FLOWI_STATIC

    fl_window_end_child_impl(void* ctx);
#else
    (api->end_child)(void* ctx);
#endif
}

// Returns true if the window is appearing after being hidden/inactive (or the first time)
FL_INLINE bool fl_window_is_appearing() {
#ifdef FLOWI_STATIC

    return fl_window_is_appearing_impl(void* ctx);
#else
    return (api->is_appearing)(void* ctx);
#endif
}

// Is current window collpased?
FL_INLINE bool fl_window_is_collapsed() {
#ifdef FLOWI_STATIC

    return fl_window_is_collapsed_impl(void* ctx);
#else
    return (api->is_collapsed)(void* ctx);
#endif
}

// is current window focused? or its root/child, depending on flags. see flags for options.
FL_INLINE bool fl_window_is_focused(FlFocusedFlags flags) {
#ifdef FLOWI_STATIC

    return fl_window_is_focused_impl(void* ctx, flags);
#else
    return (api->is_focused)(void* ctx, flags);
#endif
}

// is current window hovered (and typically: not blocked by a popup/modal)? see flags for options.
// nb: if you are trying to check whether your mouse should be dispatched to imgui or to your app,
// you should use the 'io.wantcapturemouse' boolean for that! please read the faq!
FL_INLINE bool fl_window_is_hovered(FlHoveredFlags flags) {
#ifdef FLOWI_STATIC

    return fl_window_is_hovered_impl(void* ctx, flags);
#else
    return (api->is_hovered)(void* ctx, flags);
#endif
}

// get dpi scale currently associated to the current window's viewport.
FL_INLINE float fl_window_dpi_scale() {
#ifdef FLOWI_STATIC

    return fl_window_dpi_scale_impl(void* ctx);
#else
    return (api->dpi_scale)(void* ctx);
#endif
}

// get current window position in screen space (useful if you want to do your own drawing via the drawlist api)
FL_INLINE FlVec2 fl_window_pos() {
#ifdef FLOWI_STATIC

    return fl_window_pos_impl(void* ctx);
#else
    return (api->pos)(void* ctx);
#endif
}

// get current window size
FL_INLINE FlVec2 fl_window_size() {
#ifdef FLOWI_STATIC

    return fl_window_size_impl(void* ctx);
#else
    return (api->size)(void* ctx);
#endif
}
