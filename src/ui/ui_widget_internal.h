#pragma once

// Widget-authoring internal seam: the surface the per-family widget TUs need from the ui.c core that isn't
// already in a shared header (ItemState in ui_private.h, the colour helpers in ui_variant_helpers.h,
// ui_item_behavior in ui.h). The dependency graph stays acyclic: ui_api.c -> widget TUs -> this seam ->
// ui.c core; no widget calls another widget.

#include "ui.h"
#include "ui_private.h"
#include "ui_variant_helpers.h"

// Per-item id seed for multi-item widgets (radio group, tab strip): hash a stable per-instance key (typically the
// caller's selection-storage pointer; a dock leaf passes its node id) with the item index. The seed feeds
// FlLayoutId.id, which Clay mixes with the widget name to key per-item state. The instance base is decorrelated
// (boost-style hash_combine), so adjacent keys do not produce overlapping index ranges; for a fixed key the seed
// is a bijection in index.
static inline u32 ui_item_id_seed(u32 instance_id, u32 index) {
    u32 h = instance_id + 0x9e3779b9u + (instance_id << 6) + (instance_id >> 2);
    h ^= index + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

// Near-invisible black fill that makes a layout element hit-testable: Clay emits a rectangle command for any element
// color with alpha > 0, and pointer hit-testing needs that command.
#define UI_HIT_TEST_FILL(alpha) ((FlColor) { 0, 0, 0, (alpha) })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// color with its alpha raised to at least min_alpha, RGB preserved, so a themed fill stays hit-testable.
static inline FlColor ui_hit_testable(FlColor color, u8 min_alpha) {
    if (color.a < min_alpha) {
        color.a = min_alpha;
    }
    return color;
}

// The flags every interactive widget opts into: clickable + focusable + the three state animations (hot,
// active, focus).
extern const FlItemFlags STANDARD_WIDGET_FLAGS;

// The widget-prologue result, returned by value from ui_widget_begin and ui_widget_prepare.
typedef struct UiWidgetState {
    FlLayoutId id;         // resolved id (label or override)
    FlSignal signal;       // this frame's interaction signal (hover/click/drag)
    ItemState* item;       // persisted per-item state (hover/press/focus/toggle animation), valid this frame
    FlThemeColors theme;   // resolved theme BY VALUE (override already merged; no dangling scratch pointer)
    FlWidgetColors colors; // variant bg/text/border with hover/press/focus applied; zeroed by ui_widget_begin
} UiWidgetState;

// Resolve a widget's layout id from its config override or fallback label. (body in ui.c)
FlLayoutId resolve_widget_id(FlString fallback, FlString id_override);

// Look up a widget's persisted per-item state by layout id; null when no item state exists for the id. (ui.c)
ItemState* get_item_state(FlLayoutId layout_id);

// Resolve the active theme with the widget's optional scoped override merged in: when an override applies the
// merged result is written into caller-owned scratch and returned, else the base theme pointer is. (ui.c)
const FlThemeColors* ui_resolve_widget_theme(FlUiCtx* self, const FlThemeOverride* override, FlThemeColors* scratch);

// How a tab strip sizes its tabs and paints the selected one. A zeroed layout is a plain tab bar: a row of
// fit-to-label buttons. The dock leaf header fills it in as a title bar: its tabs divide the leaf's full width at a
// title-bar height, and the selected one is a tinted surface rather than an accent fill.
typedef struct UiTabStripLayout {
    // Width band one HORIZONTAL tab lays out in, in reference pixels. Without stretch the tab fits its label
    // inside the band (0 max = uncapped); under stretch only the min survives, as the floor a share shrinks
    // to. A vertical strip ignores both.
    f32 min_width;
    f32 max_width;
    // Tab height in reference pixels; 0 takes the FlWidgetHeight_MD default.
    f32 height;
    // Tabs divide the strip's main axis instead of fitting their labels, and their labels sit left rather
    // than centred - a lone tab then spans the strip as a title bar.
    bool stretch;
    // Variant the selected tab paints with; Solid, the zero, is the accent fill. Unselected tabs stay Ghost.
    FlWidgetVariant selected_variant;
} UiTabStripLayout;

// Horizontal/vertical tab strip - the core of ui_tab_bar, also driven by the dock leaf header
// (ui/dock/dock_layout.c).
// Renders count tabs labelled by labels, highlighting selected, with each tab's id seeded from
// ui_item_id_seed(instance_id, i) (the public bar uses the selection pointer, a dock leaf its node id). Resolves
// the press in a behaviour pass before drawing (activate-on-press), so the pressed tab paints selected this frame.
// Owns no selection storage: returns the index that should now be selected (the pressed/activated tab, else
// selected unchanged).
// cfg carries the orientation, the geometry knobs and widget's focusable/tab_index for keyboard
// reachability; theme is the already-resolved palette; layout is the shape below.
//
// out_dragged (optional) opts the strip into the pull-out gesture: the tabs become draggable behind the drag
// threshold and the index of the tab being dragged is written back (-1 when none). Pass null for a plain tab
// bar, whose tabs stay click-only.
int ui_tab_strip(FlString name, u32 instance_id, const FlString* labels, int count, int selected, FlTabBarConfig cfg,
                 const FlThemeColors* theme, UiTabStripLayout layout, int* out_dragged);

// Shared widget prologue for custom-coloured widgets: resolve id + dispatch the shared interaction behaviour +
// look up item-state + resolve theme. Leaves UiWidgetState.colors zeroed; no variant colours are calculated.
// (body in ui_widget_common.c)
UiWidgetState ui_widget_begin(FlString label, FlWidgetConfig cfg, FlItemFlags flags);

// Widget prologue for variant-coloured widgets: ui_widget_begin plus the variant colours, resolved once from
// the item state and resolved theme. (body in ui_widget_common.c)
UiWidgetState ui_widget_prepare(FlString label, FlWidgetConfig cfg, FlItemFlags flags, FlWidgetVariant variant,
                                bool animate_text);
