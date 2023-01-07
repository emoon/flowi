typedef struct FlItemApi {
    struct FlInternalData* priv;
    bool (*is_hovered)(struct FlInternalData* priv, FlHoveredFlags flags);
    bool (*is_active)(struct FlInternalData* priv);
    bool (*is_focused)(struct FlInternalData* priv);
    bool (*is_clicked)(struct FlInternalData* priv);
    bool (*is_visible)(struct FlInternalData* priv);
    bool (*is_edited)(struct FlInternalData* priv);
    bool (*is_activated)(struct FlInternalData* priv);
    bool (*is_deactivated)(struct FlInternalData* priv);
    bool (*is_deactivated_after_edit)(struct FlInternalData* priv);
    bool (*is_toggled_open)(struct FlInternalData* priv);
    bool (*is_any_hovered)(struct FlInternalData* priv);
    bool (*is_any_active)(struct FlInternalData* priv);
    bool (*is_any_focused)(struct FlInternalData* priv);
    FlVec2 (*get_rect_min)(struct FlInternalData* priv);
    FlVec2 (*get_rect_max)(struct FlInternalData* priv);
    FlVec2 (*get_rect_size)(struct FlInternalData* priv);
    void (*set_allow_overlap)(struct FlInternalData* priv);
} FlItemApi;

// Is the last item hovered? (and usable, aka not blocked by a popup, etc.). See ImGuiHoveredFlags for more options.
FL_INLINE bool fl_item_is_hovered(struct FlItemApi* api, FlHoveredFlags flags) {
    return (api->is_hovered)(api->priv, flags);
}

// Is the last item active? (e.g. button being held, text field being edited. This will continuously return true while
// holding mouse button on an item. _s that don't interact will always return false)
FL_INLINE bool fl_item_is_active(struct FlItemApi* api) {
    return (api->is_active)(api->priv);
}

// Is the last item focused for keyboard/gamepad navigation?
FL_INLINE bool fl_item_is_focused(struct FlItemApi* api) {
    return (api->is_focused)(api->priv);
}

// Is the last item hovered and mouse clicked on? (**)  == IsMouseClicked(mouse_button) && Is_Hovered()Important. (**)
// this is NOT equivalent to the behavior of e.g. Button(). Read comments in function definition.
FL_INLINE bool fl_item_is_clicked(struct FlItemApi* api) {
    return (api->is_clicked)(api->priv);
}

// Is the last item visible? (items may be out of sight because of clipping/scrolling)
FL_INLINE bool fl_item_is_visible(struct FlItemApi* api) {
    return (api->is_visible)(api->priv);
}

// Did the last item modify its underlying value this frame? or was pressed? This is generally the same as the "bool"
// return value of many widgets.
FL_INLINE bool fl_item_is_edited(struct FlItemApi* api) {
    return (api->is_edited)(api->priv);
}

// Was the last item just made active (item was previously inactive).
FL_INLINE bool fl_item_is_activated(struct FlItemApi* api) {
    return (api->is_activated)(api->priv);
}

// Was the last item just made inactive (item was previously active). Useful for Undo/Redo patterns with widgets that
// require continuous editing.
FL_INLINE bool fl_item_is_deactivated(struct FlItemApi* api) {
    return (api->is_deactivated)(api->priv);
}

// Was the last item just made inactive and made a value change when it was active? (e.g. Slider/Drag moved). Useful for
// Undo/Redo patterns with widgets that require continuous editing. Note that you may get false positives (some widgets
// such as Combo()/ListBox()/Selectable() will return true even when clicking an already selected item).
FL_INLINE bool fl_item_is_deactivated_after_edit(struct FlItemApi* api) {
    return (api->is_deactivated_after_edit)(api->priv);
}

// Was the last item open state toggled? set by TreeNode().
FL_INLINE bool fl_item_is_toggled_open(struct FlItemApi* api) {
    return (api->is_toggled_open)(api->priv);
}

// Is any item hovered?
FL_INLINE bool fl_item_is_any_hovered(struct FlItemApi* api) {
    return (api->is_any_hovered)(api->priv);
}

// Is any item active?
FL_INLINE bool fl_item_is_any_active(struct FlItemApi* api) {
    return (api->is_any_active)(api->priv);
}

// Is any item focused?
FL_INLINE bool fl_item_is_any_focused(struct FlItemApi* api) {
    return (api->is_any_focused)(api->priv);
}

// Get upper-left bounding rectangle of the last item (screen space)
FL_INLINE FlVec2 fl_item_get_rect_min(struct FlItemApi* api) {
    return (api->get_rect_min)(api->priv);
}

// Get lower-right bounding rectangle of the last item (screen space)
FL_INLINE FlVec2 fl_item_get_rect_max(struct FlItemApi* api) {
    return (api->get_rect_max)(api->priv);
}

// Get size of last item
FL_INLINE FlVec2 fl_item_get_rect_size(struct FlItemApi* api) {
    return (api->get_rect_size)(api->priv);
}

// Allow last item to be overlapped by a subsequent item. sometimes useful with invisible buttons, selectables, etc. to
// catch unused area.
FL_INLINE void fl_item_set_allow_overlap(struct FlItemApi* api) {
    (api->set_allow_overlap)(api->priv);
}
