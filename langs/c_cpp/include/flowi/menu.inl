typedef struct FlMenuApi {
    struct FlInternalData* priv;
    bool (*begin_bar)(struct FlInternalData* priv);
    void (*end_bar)(struct FlInternalData* priv);
    bool (*begin_main_bar)(struct FlInternalData* priv);
    void (*end_main_bar)(struct FlInternalData* priv);
    bool (*begin)(struct FlInternalData* priv, FlString label, bool enabled);
    void (*end)(struct FlInternalData* priv);
    bool (*item)(struct FlInternalData* priv, FlString label);
    bool (*item_ex)(struct FlInternalData* priv, FlString label, FlString shortcut, bool selected, bool enabled);
    bool (*item_toggle)(struct FlInternalData* priv, FlString label, FlString shortcut, bool* selected, bool enabled);
} FlMenuApi;

extern FlMenuApi* g_flowi_menu_api;

#ifdef FLOWI_STATIC
bool fl_menu_begin_bar_impl(struct FlInternalData* priv);
void fl_menu_end_bar_impl(struct FlInternalData* priv);
bool fl_menu_begin_main_bar_impl(struct FlInternalData* priv);
void fl_menu_end_main_bar_impl(struct FlInternalData* priv);
bool fl_menu_begin_impl(struct FlInternalData* priv, FlString label, bool enabled);
void fl_menu_end_impl(struct FlInternalData* priv);
bool fl_menu_item_impl(struct FlInternalData* priv, FlString label);
bool fl_menu_item_ex_impl(struct FlInternalData* priv, FlString label, FlString shortcut, bool selected, bool enabled);
bool fl_menu_item_toggle_impl(struct FlInternalData* priv, FlString label, FlString shortcut, bool* selected,
                              bool enabled);
#endif

// Append to menu-bar of current window (requires [WindowFlags::MENU_BAR] flag set on parent window).
FL_INLINE bool fl_menu_begin_bar() {
#ifdef FLOWI_STATIC
    return fl_menu_begin_bar_impl(g_flowi_menu_api->priv);
#else
    return (g_flowi_menu_api->begin_bar)(g_flowi_menu_api->priv);
#endif
}

// only call end_bar() if begin_bar() returns true!
FL_INLINE void fl_menu_end_bar() {
#ifdef FLOWI_STATIC
    fl_menu_end_bar_impl(g_flowi_menu_api->priv);
#else
    (g_flowi_menu_api->end_bar)(g_flowi_menu_api->priv);
#endif
}

// create and append to a full screen menu-bar.
FL_INLINE bool fl_menu_begin_main_bar() {
#ifdef FLOWI_STATIC
    return fl_menu_begin_main_bar_impl(g_flowi_menu_api->priv);
#else
    return (g_flowi_menu_api->begin_main_bar)(g_flowi_menu_api->priv);
#endif
}

// only call end_main_bar() if begin_main_bar() returns true!
FL_INLINE void fl_menu_end_main_bar() {
#ifdef FLOWI_STATIC
    fl_menu_end_main_bar_impl(g_flowi_menu_api->priv);
#else
    (g_flowi_menu_api->end_main_bar)(g_flowi_menu_api->priv);
#endif
}

// create a sub-menu entry. only call EndMenu() if this returns true!
FL_INLINE bool fl_menu_begin(const char* label, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_menu_begin_impl(g_flowi_menu_api->priv, label_, enabled);
#else
    return (g_flowi_menu_api->begin)(g_flowi_menu_api->priv, label_, enabled);
#endif
}

// only call end_menu() if begin_menu() returns true!
FL_INLINE void fl_menu_end() {
#ifdef FLOWI_STATIC
    fl_menu_end_impl(g_flowi_menu_api->priv);
#else
    (g_flowi_menu_api->end)(g_flowi_menu_api->priv);
#endif
}

// return true when activated.
FL_INLINE bool fl_menu_item(const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC
    return fl_menu_item_impl(g_flowi_menu_api->priv, label_);
#else
    return (g_flowi_menu_api->item)(g_flowi_menu_api->priv, label_);
#endif
}

// return true when activated. Includes some extra info such as shortcut, etc
FL_INLINE bool fl_menu_item_ex(const char* label, const char* shortcut, bool selected, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString shortcut_ = fl_cstr_to_flstring(shortcut);
#ifdef FLOWI_STATIC
    return fl_menu_item_ex_impl(g_flowi_menu_api->priv, label_, shortcut_, selected, enabled);
#else
    return (g_flowi_menu_api->item_ex)(g_flowi_menu_api->priv, label_, shortcut_, selected, enabled);
#endif
}

// return true when activated + toggle selected
FL_INLINE bool fl_menu_item_toggle(const char* label, const char* shortcut, bool* selected, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString shortcut_ = fl_cstr_to_flstring(shortcut);
#ifdef FLOWI_STATIC
    return fl_menu_item_toggle_impl(g_flowi_menu_api->priv, label_, shortcut_, selected, enabled);
#else
    return (g_flowi_menu_api->item_toggle)(g_flowi_menu_api->priv, label_, shortcut_, selected, enabled);
#endif
}
