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

extern FlWindowApi* g_flowi_window_api;

#ifdef FLOWI_STATIC
void fl_window_set_pos_impl(struct FlInternalData* priv, FlVec2 pos);
bool fl_window_begin_impl(struct FlInternalData* priv, FlString name, FlWindowFlags flags);
void fl_window_end_impl(struct FlInternalData* priv);
bool fl_window_begin_child_impl(struct FlInternalData* priv, FlString id, FlVec2 size, bool border,
                                FlWindowFlags flags);
void fl_window_end_child_impl(struct FlInternalData* priv);
bool fl_window_is_appearing_impl(struct FlInternalData* priv);
bool fl_window_is_collapsed_impl(struct FlInternalData* priv);
bool fl_window_is_focused_impl(struct FlInternalData* priv, FlFocusedFlags flags);
bool fl_window_is_hovered_impl(struct FlInternalData* priv, FlHoveredFlags flags);
float fl_window_dpi_scale_impl(struct FlInternalData* priv);
FlVec2 fl_window_pos_impl(struct FlInternalData* priv);
FlVec2 fl_window_size_impl(struct FlInternalData* priv);
#endif

// Sets the position of the next window, call before begin()
FL_INLINE void fl_window_set_pos(FlVec2 pos) {
#ifdef FLOWI_STATIC
    fl_window_set_pos_impl(g_flowi_window_api->priv, pos);
#else
    (g_flowi_window_api->set_pos)(g_flowi_window_api->priv, pos);
#endif
}

// Always call a matching end() for each begin() call, regardless of its return value!
FL_INLINE bool fl_window_begin(const char* name, FlWindowFlags flags) {
    FlString name_ = fl_cstr_to_flstring(name);
#ifdef FLOWI_STATIC
    return fl_window_begin_impl(g_flowi_window_api->priv, name_, flags);
#else
    return (g_flowi_window_api->begin)(g_flowi_window_api->priv, name_, flags);
#endif
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_window_end() {
#ifdef FLOWI_STATIC
    fl_window_end_impl(g_flowi_window_api->priv);
#else
    (g_flowi_window_api->end)(g_flowi_window_api->priv);
#endif
}

// Call between begin() and end() to create a child window. Child windows can embed their own child.
FL_INLINE bool fl_window_begin_child(const char* id, FlVec2 size, bool border, FlWindowFlags flags) {
    FlString id_ = fl_cstr_to_flstring(id);
#ifdef FLOWI_STATIC
    return fl_window_begin_child_impl(g_flowi_window_api->priv, id_, size, border, flags);
#else
    return (g_flowi_window_api->begin_child)(g_flowi_window_api->priv, id_, size, border, flags);
#endif
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_window_end_child() {
#ifdef FLOWI_STATIC
    fl_window_end_child_impl(g_flowi_window_api->priv);
#else
    (g_flowi_window_api->end_child)(g_flowi_window_api->priv);
#endif
}

// Returns true if the window is appearing after being hidden/inactive (or the first time)
FL_INLINE bool fl_window_is_appearing() {
#ifdef FLOWI_STATIC
    return fl_window_is_appearing_impl(g_flowi_window_api->priv);
#else
    return (g_flowi_window_api->is_appearing)(g_flowi_window_api->priv);
#endif
}

// Is current window collpased?
FL_INLINE bool fl_window_is_collapsed() {
#ifdef FLOWI_STATIC
    return fl_window_is_collapsed_impl(g_flowi_window_api->priv);
#else
    return (g_flowi_window_api->is_collapsed)(g_flowi_window_api->priv);
#endif
}

// is current window focused? or its root/child, depending on flags. see flags for options.
FL_INLINE bool fl_window_is_focused(FlFocusedFlags flags) {
#ifdef FLOWI_STATIC
    return fl_window_is_focused_impl(g_flowi_window_api->priv, flags);
#else
    return (g_flowi_window_api->is_focused)(g_flowi_window_api->priv, flags);
#endif
}

// is current window hovered (and typically: not blocked by a popup/modal)? see flags for options.
// nb: if you are trying to check whether your mouse should be dispatched to imgui or to your app,
// you should use the 'io.wantcapturemouse' boolean for that! please read the faq!
FL_INLINE bool fl_window_is_hovered(FlHoveredFlags flags) {
#ifdef FLOWI_STATIC
    return fl_window_is_hovered_impl(g_flowi_window_api->priv, flags);
#else
    return (g_flowi_window_api->is_hovered)(g_flowi_window_api->priv, flags);
#endif
}

// get dpi scale currently associated to the current window's viewport.
FL_INLINE float fl_window_dpi_scale() {
#ifdef FLOWI_STATIC
    return fl_window_dpi_scale_impl(g_flowi_window_api->priv);
#else
    return (g_flowi_window_api->dpi_scale)(g_flowi_window_api->priv);
#endif
}

// get current window position in screen space (useful if you want to do your own drawing via the drawlist api)
FL_INLINE FlVec2 fl_window_pos() {
#ifdef FLOWI_STATIC
    return fl_window_pos_impl(g_flowi_window_api->priv);
#else
    return (g_flowi_window_api->pos)(g_flowi_window_api->priv);
#endif
}

// get current window size
FL_INLINE FlVec2 fl_window_size() {
#ifdef FLOWI_STATIC
    return fl_window_size_impl(g_flowi_window_api->priv);
#else
    return (g_flowi_window_api->size)(g_flowi_window_api->priv);
#endif
}
