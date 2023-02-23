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
FL_INLINE bool fl_menu_begin_bar(struct FlMenuApi* api) {
    return (api->begin_bar)(api->priv);
}

// only call end_bar() if begin_bar() returns true!
FL_INLINE void fl_menu_end_bar(struct FlMenuApi* api) {
    (api->end_bar)(api->priv);
}

// create and append to a full screen menu-bar.
FL_INLINE bool fl_menu_begin_main_bar(struct FlMenuApi* api) {
    return (api->begin_main_bar)(api->priv);
}

// only call end_main_bar() if begin_main_bar() returns true!
FL_INLINE void fl_menu_end_main_bar(struct FlMenuApi* api) {
    (api->end_main_bar)(api->priv);
}

// create a sub-menu entry. only call EndMenu() if this returns true!
FL_INLINE bool fl_menu_begin(struct FlMenuApi* api, const char* label, bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->begin)(api->priv, label_, enabled);
}

// only call end_menu() if begin_menu() returns true!
FL_INLINE void fl_menu_end(struct FlMenuApi* api) {
    (api->end)(api->priv);
}

// return true when activated.
FL_INLINE bool fl_menu_item(struct FlMenuApi* api, const char* label) {
    FlString label_ = fl_cstr_to_flstring(label);
    return (api->item)(api->priv, label_);
}

// return true when activated. Includes some extra info such as shortcut, etc
FL_INLINE bool fl_menu_item_ex(struct FlMenuApi* api, const char* label, const char* shortcut, bool selected,
                               bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString shortcut_ = fl_cstr_to_flstring(shortcut);
    return (api->item_ex)(api->priv, label_, shortcut_, selected, enabled);
}

// return true when activated + toggle selected
FL_INLINE bool fl_menu_item_toggle(struct FlMenuApi* api, const char* label, const char* shortcut, bool* selected,
                                   bool enabled) {
    FlString label_ = fl_cstr_to_flstring(label);
    FlString shortcut_ = fl_cstr_to_flstring(shortcut);
    return (api->item_toggle)(api->priv, label_, shortcut_, selected, enabled);
}
