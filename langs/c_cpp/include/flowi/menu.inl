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

// Append to menu-bar of current window (requires [WindowFlags::MENU_BAR] flag set on parent window).
FL_INLINE bool fl_menu_begin_bar() {
#ifdef FLOWI_STATIC

    return fl_menu_begin_bar_impl(void* ctx);
#else
    return (api->begin_bar)(void* ctx);
#endif
}

// only call end_bar() if begin_bar() returns true!
FL_INLINE void fl_menu_end_bar() {
#ifdef FLOWI_STATIC

    fl_menu_end_bar_impl(void* ctx);
#else
    (api->end_bar)(void* ctx);
#endif
}

// create and append to a full screen menu-bar.
FL_INLINE bool fl_menu_begin_main_bar() {
#ifdef FLOWI_STATIC

    return fl_menu_begin_main_bar_impl(void* ctx);
#else
    return (api->begin_main_bar)(void* ctx);
#endif
}

// only call end_main_bar() if begin_main_bar() returns true!
FL_INLINE void fl_menu_end_main_bar() {
#ifdef FLOWI_STATIC

    fl_menu_end_main_bar_impl(void* ctx);
#else
    (api->end_main_bar)(void* ctx);
#endif
}

// create a sub-menu entry. only call EndMenu() if this returns true!
FL_INLINE bool fl_menu_begin(const char* label, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_menu_begin_impl(void* ctx, label_, enabled);
#else
    return (api->begin)(void* ctx, label_, enabled);
#endif
}

// only call end_menu() if begin_menu() returns true!
FL_INLINE void fl_menu_end() {
#ifdef FLOWI_STATIC

    fl_menu_end_impl(void* ctx);
#else
    (api->end)(void* ctx);
#endif
}

// return true when activated.
FL_INLINE bool fl_menu_item(const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
#ifdef FLOWI_STATIC

        return fl_menu_item_impl(void* ctx, label_);
#else
    return (api->item)(void* ctx, label_);
#endif
}

// return true when activated. Includes some extra info such as shortcut, etc
FL_INLINE bool fl_menu_item_ex(const char* label, const char* shortcut, bool selected, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString shortcut_ = fl_cstr_to_flstring(shortcut);
#ifdef FLOWI_STATIC

        return fl_menu_item_ex_impl(void* ctx, label_, shortcut_, selected, enabled);
#else
    return (api->item_ex)(void* ctx, label_, shortcut_, selected, enabled);
#endif
}

// return true when activated + toggle selected
FL_INLINE bool fl_menu_item_toggle(const char* label, const char* shortcut, bool* selected, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString shortcut_ = fl_cstr_to_flstring(shortcut);
#ifdef FLOWI_STATIC

        return fl_menu_item_toggle_impl(void* ctx, label_, shortcut_, selected, enabled);
#else
    return (api->item_toggle)(void* ctx, label_, shortcut_, selected, enabled);
#endif
}
