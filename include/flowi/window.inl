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
FL_INLINE void fl_window_set_pos(struct FlWindowApi* api, FlVec2 pos) {
    (api->set_pos)(api->priv, pos);
}

// Always call a matching end() for each begin() call, regardless of its return value!
FL_INLINE bool fl_window_begin(struct FlWindowApi* api, const char* name, FlWindowFlags flags) {
    FlString name_ = fl_cstr_to_flstring(name);
    return (api->begin)(api->priv, name_, flags);
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_window_end(struct FlWindowApi* api) {
    (api->end)(api->priv);
}

// Call between begin() and end() to create a child window. Child windows can embed their own child.
FL_INLINE bool fl_window_begin_child(struct FlWindowApi* api, const char* id, FlVec2 size, bool border,
                                     FlWindowFlags flags) {
    FlString id_ = fl_cstr_to_flstring(id);
    return (api->begin_child)(api->priv, id_, size, border, flags);
}

// End call for various types such as windows, lists, etc.
FL_INLINE void fl_window_end_child(struct FlWindowApi* api) {
    (api->end_child)(api->priv);
}

// Returns true if the window is appearing after being hidden/inactive (or the first time)
FL_INLINE bool fl_window_is_appearing(struct FlWindowApi* api) {
    return (api->is_appearing)(api->priv);
}

// Is current window collpased?
FL_INLINE bool fl_window_is_collapsed(struct FlWindowApi* api) {
    return (api->is_collapsed)(api->priv);
}

// is current window focused? or its root/child, depending on flags. see flags for options.
FL_INLINE bool fl_window_is_focused(struct FlWindowApi* api, FlFocusedFlags flags) {
    return (api->is_focused)(api->priv, flags);
}

// is current window hovered (and typically: not blocked by a popup/modal)? see flags for options.
// nb: if you are trying to check whether your mouse should be dispatched to imgui or to your app,
// you should use the 'io.wantcapturemouse' boolean for that! please read the faq!
FL_INLINE bool fl_window_is_hovered(struct FlWindowApi* api, FlHoveredFlags flags) {
    return (api->is_hovered)(api->priv, flags);
}

// get dpi scale currently associated to the current window's viewport.
FL_INLINE float fl_window_dpi_scale(struct FlWindowApi* api) {
    return (api->dpi_scale)(api->priv);
}

// get current window position in screen space (useful if you want to do your own drawing via the drawlist api)
FL_INLINE FlVec2 fl_window_pos(struct FlWindowApi* api) {
    return (api->pos)(api->priv);
}

// get current window size
FL_INLINE FlVec2 fl_window_size(struct FlWindowApi* api) {
    return (api->size)(api->priv);
}
