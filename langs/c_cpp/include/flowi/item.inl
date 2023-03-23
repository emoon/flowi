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
FL_INLINE bool fl_item_is_hovered(FlHoveredFlags flags) {
#ifdef FLOWI_STATIC

    return fl_item_is_hovered_impl(void* ctx, flags);
#else
    return (api->is_hovered)(void* ctx, flags);
#endif
}

// Is the last item active? (e.g. button being held, text field being edited. This will continuously return true while
// holding mouse button on an item. _s that don't interact will always return false)
FL_INLINE bool fl_item_is_active() {
#ifdef FLOWI_STATIC

    return fl_item_is_active_impl(void* ctx);
#else
    return (api->is_active)(void* ctx);
#endif
}

// Is the last item focused for keyboard/gamepad navigation?
FL_INLINE bool fl_item_is_focused() {
#ifdef FLOWI_STATIC

    return fl_item_is_focused_impl(void* ctx);
#else
    return (api->is_focused)(void* ctx);
#endif
}

// Is the last item hovered and mouse clicked on? (**)  == IsMouseClicked(mouse_button) && Is_Hovered()Important. (**)
// this is NOT equivalent to the behavior of e.g. Button(). Read comments in function definition.
FL_INLINE bool fl_item_is_clicked() {
#ifdef FLOWI_STATIC

    return fl_item_is_clicked_impl(void* ctx);
#else
    return (api->is_clicked)(void* ctx);
#endif
}

// Is the last item visible? (items may be out of sight because of clipping/scrolling)
FL_INLINE bool fl_item_is_visible() {
#ifdef FLOWI_STATIC

    return fl_item_is_visible_impl(void* ctx);
#else
    return (api->is_visible)(void* ctx);
#endif
}

// Did the last item modify its underlying value this frame? or was pressed? This is generally the same as the "bool"
// return value of many widgets.
FL_INLINE bool fl_item_is_edited() {
#ifdef FLOWI_STATIC

    return fl_item_is_edited_impl(void* ctx);
#else
    return (api->is_edited)(void* ctx);
#endif
}

// Was the last item just made active (item was previously inactive).
FL_INLINE bool fl_item_is_activated() {
#ifdef FLOWI_STATIC

    return fl_item_is_activated_impl(void* ctx);
#else
    return (api->is_activated)(void* ctx);
#endif
}

// Was the last item just made inactive (item was previously active). Useful for Undo/Redo patterns with widgets that
// require continuous editing.
FL_INLINE bool fl_item_is_deactivated() {
#ifdef FLOWI_STATIC

    return fl_item_is_deactivated_impl(void* ctx);
#else
    return (api->is_deactivated)(void* ctx);
#endif
}

// Was the last item just made inactive and made a value change when it was active? (e.g. Slider/Drag moved). Useful for
// Undo/Redo patterns with widgets that require continuous editing. Note that you may get false positives (some widgets
// such as Combo()/ListBox()/Selectable() will return true even when clicking an already selected item).
FL_INLINE bool fl_item_is_deactivated_after_edit() {
#ifdef FLOWI_STATIC

    return fl_item_is_deactivated_after_edit_impl(void* ctx);
#else
    return (api->is_deactivated_after_edit)(void* ctx);
#endif
}

// Was the last item open state toggled? set by TreeNode().
FL_INLINE bool fl_item_is_toggled_open() {
#ifdef FLOWI_STATIC

    return fl_item_is_toggled_open_impl(void* ctx);
#else
    return (api->is_toggled_open)(void* ctx);
#endif
}

// Is any item hovered?
FL_INLINE bool fl_item_is_any_hovered() {
#ifdef FLOWI_STATIC

    return fl_item_is_any_hovered_impl(void* ctx);
#else
    return (api->is_any_hovered)(void* ctx);
#endif
}

// Is any item active?
FL_INLINE bool fl_item_is_any_active() {
#ifdef FLOWI_STATIC

    return fl_item_is_any_active_impl(void* ctx);
#else
    return (api->is_any_active)(void* ctx);
#endif
}

// Is any item focused?
FL_INLINE bool fl_item_is_any_focused() {
#ifdef FLOWI_STATIC

    return fl_item_is_any_focused_impl(void* ctx);
#else
    return (api->is_any_focused)(void* ctx);
#endif
}

// Get upper-left bounding rectangle of the last item (screen space)
FL_INLINE FlVec2 fl_item_get_rect_min() {
#ifdef FLOWI_STATIC

    return fl_item_get_rect_min_impl(void* ctx);
#else
    return (api->get_rect_min)(void* ctx);
#endif
}

// Get lower-right bounding rectangle of the last item (screen space)
FL_INLINE FlVec2 fl_item_get_rect_max() {
#ifdef FLOWI_STATIC

    return fl_item_get_rect_max_impl(void* ctx);
#else
    return (api->get_rect_max)(void* ctx);
#endif
}

// Get size of last item
FL_INLINE FlVec2 fl_item_get_rect_size() {
#ifdef FLOWI_STATIC

    return fl_item_get_rect_size_impl(void* ctx);
#else
    return (api->get_rect_size)(void* ctx);
#endif
}

// Allow last item to be overlapped by a subsequent item. sometimes useful with invisible buttons, selectables, etc. to
// catch unused area.
FL_INLINE void fl_item_set_allow_overlap() {
#ifdef FLOWI_STATIC

    fl_item_set_allow_overlap_impl(void* ctx);
#else
    (api->set_allow_overlap)(void* ctx);
#endif
}
