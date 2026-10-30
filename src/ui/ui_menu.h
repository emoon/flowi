#pragma once

#include <ui/ui_types.h> // FlString/bool - base types the generated headers below rely on
#include <flowi/core/platform.h>
#include <flowi/ui/layout.h> // @generated FlLayoutId (api_gen)
#include <flowi/ui/menu.h>   // @generated FlMenuRegionConfig (api_gen)
#include <flowi/ui/signal.h> // @generated FlSignal (api_gen)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu (context menu) API
//
// A flat, immediate-mode context menu, built by emitting items with ui_menu_item between ui_menu_begin/end (the
// ui_menu scoped macro), so heterogeneous and custom content can sit between items.
//
// Lifecycle:
//   ui_menu_region(region_id, menu_id) { ...target content... }   // right-click inside opens menu_id at the cursor
//   ui_menu(menu_id, .min_width = 220) {                           // body runs only while the menu is open
//       if (ui_menu_item(FL_S("Cut"),  .shortcut = FL_S("Ctrl+X")).clicked) { ... }
//       if (ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C")).clicked) { ... }
//       ui_menu_item(FL_S("Paste"), .disabled = true);
//   }
//
// Dismissal: ESC or a click outside the union of the menu's rects closes the chain. An action item closes the chain
// on activation by default (per-item close policy). The activating item's FlSignal reports .clicked the same frame
// the menu tears down.

// Open the menu chain identified by menu_id at an absolute screen point (captured once - no live-follow).
// Idempotent re-anchor while already open. Normally called for you by ui_menu_region; exposed for callers that want
// to open a menu from a custom trigger.
void ui_menu_open_impl(FlLayoutId menu_id, FlVec2 point);

// Linear blend of two colors, truncating each channel (the menu's highlight blend).
FlColor ui_menu_blend(FlColor a, FlColor b, float t);

// Close the menu chain identified by menu_id (no-op if it is not the open chain).
void ui_menu_close_impl(FlLayoutId menu_id);

// True while the menu chain identified by menu_id is open.
bool ui_menu_is_open_impl(FlLayoutId menu_id);

// Begin the menu: returns true (and opens the floating panel) only while the menu is open. Drives self-owned
// dismissal (ESC / click-outside). Pair with ui_menu_end_impl only when it returned true (the ui_menu macro does
// this for you).
bool ui_menu_begin_impl(FlLayoutId menu_id, FlMenuConfig config);

// Emit one action item; returns its interaction signal (.clicked true the frame it activates).
FlSignal ui_menu_item_impl(FlString label, FlMenuItemConfig config);

// Emit a checkable item: a leaf that toggles `*checked` on activation and shows a check marker in
// a leading gutter while `*checked` is true. Keep-open by default (so several toggles can be flipped without
// reopening) - pass .close_policy = FlMenuClosePolicy_Close to force a dismiss. Returns the row's signal (.clicked
// the frame it toggles). A null checked shows no mark and toggles nothing (still a live, hoverable row).
FlSignal ui_menu_check_impl(FlString label, bool* checked, FlMenuItemConfig config);

// Emit a radio item: one option in a group whose selection state is the caller's `*selected`.
// Activation sets `*selected = value`; the bullet marker shows while `*selected == value`, so a group of radio items
// sharing one selected is mutually exclusive for free. Keep-open by default (same override as ui_menu_check).
// Returns the row's signal. A null selected shows no mark and selects nothing.
FlSignal ui_menu_radio_impl(FlString label, int* selected, int value, FlMenuItemConfig config);

// Emit a separator: a non-interactive horizontal rule between items. It occupies a row but is
// skipped by keyboard navigation and never activates; clicking it stays inside the menu (it is part of the surface,
// not click-outside).
void ui_menu_separator_impl(void);

// Emit a section header: a non-interactive caption row (dimmed, left-aligned). Like a separator
// it occupies a row, is skipped by navigation, and never activates.
void ui_menu_header_impl(FlString label);

// Begin a submenu parent. Emits the parent row (a pure container - chevron, never an action
// signal) at the current level, and returns true ONLY while this submenu is the one expanded at its level, opening a
// nested floating panel beside the parent (right by default, flipped left on overflow). The body then emits the child
// items into the deeper level; pair ui_submenu_end_impl with it only when it returned true (the ui_submenu macro does
// this for you). A cold submenu opens on hover-dwell, click, or Right-arrow; Left-arrow / ESC collapse it one level.
bool ui_submenu_begin_impl(FlString label, FlSubmenuConfig config);

// End the submenu panel opened by ui_submenu_begin_impl (closes the nested floating panel, pops one build level).
void ui_submenu_end_impl(void);

// End the menu panel opened by ui_menu_begin_impl.
void ui_menu_end_impl(void);

// Right-click region: wrap a target area; a right-click inside opens menu_id at the cursor. Opts into
// FlItemFlags_right_click for the caller. Returns the region's signal.
FlSignal ui_menu_region_begin_impl(FlLayoutId region_id, FlLayoutId menu_id, FlMenuRegionConfig config);
void ui_menu_region_end_impl(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu bar - the persistent File/Edit/View strip.
//
// The bar is the core's level 0, rendered as a horizontal strip instead of a floating panel; each top's dropdown is
// level 1, anchored under its bar button (element-anchor). A top opens on CLICK (never the cold hover-dwell the
// submenu uses). Once a dropdown is open, hovering a sibling top switches the open root instantly; Left/Right move
// between tops and Down opens the highlighted one.
//
//   ui_menubar(bar_id) {                              // the strip; the body always runs (tops are always visible)
//       ui_menubar_menu(FL_S("File")) {                  // a top; the body runs only while THIS dropdown is open
//           if (ui_menu_item(FL_S("Open")).clicked) { ... }
//       }
//       ui_menubar_menu(FL_S("Edit")) { ui_menu_item(FL_S("Undo")); }
//   }

// Begin the bar strip and run its per-frame input (click-open, hover-switch-while-open, Left/Right/Down/ESC). Always
// opens the horizontal strip container, so the body (which emits the tops) runs every frame. Pair with
// ui_menubar_end_impl (the ui_menubar macro does this for you).
void ui_menubar_begin_impl(FlLayoutId bar_id, FlMenuBarConfig config);
void ui_menubar_end_impl(void);

// Emit one top-level entry into the strip and return true ONLY while this top's dropdown is open, opening the dropdown
// panel anchored under the button. The body then emits the dropdown's items into the
// deeper level; pair ui_menubar_menu_end_impl with it only when it returned true (the ui_menubar_menu macro does this).
bool ui_menubar_menu_begin_impl(FlString label, FlMenuBarMenuConfig config);
void ui_menubar_menu_end_impl(void);

// True while the bar identified by bar_id owns the open chain (a dropdown is open, or the bar is keyboard-focused
// after an ESC collapsed the dropdown back to the strip).
bool ui_menubar_is_open_impl(FlLayoutId bar_id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scope-based macros

static int UI_MENU_LATCH UNUSED_VARIABLE = 0;
static int UI_MENU_SUBMENU_LATCH UNUSED_VARIABLE = 0;
static int UI_MENU_REGION_LATCH UNUSED_VARIABLE = 0;
static int UI_MENUBAR_LATCH UNUSED_VARIABLE = 0;
static int UI_MENUBAR_MENU_LATCH UNUSED_VARIABLE = 0;

// clang-format off
// Runs the body (and ui_menu_end) only while the menu is open; a closed menu skips the body and emits nothing.
#define ui_menu(menu_id, ...) \
    for ( \
         UI_MENU_LATCH = (ui_menu_begin_impl(menu_id, (FlMenuConfig) { __VA_ARGS__ }) ? 0 : 1); \
         UI_MENU_LATCH < 1; \
         UI_MENU_LATCH = 1, ui_menu_end_impl())

#define ui_menu_item(label, ...) ui_menu_item_impl(label, (FlMenuItemConfig) { __VA_ARGS__ })

// Stateful item kinds - a checkable toggle over a bool*, and a radio option over a shared int* group state.
// Both keep the menu open on activation by default (override with .close_policy).
#define ui_menu_check(label, checked, ...) ui_menu_check_impl(label, checked, (FlMenuItemConfig) { __VA_ARGS__ })
#define ui_menu_radio(label, selected, value, ...) \
    ui_menu_radio_impl(label, selected, value, (FlMenuItemConfig) { __VA_ARGS__ })

// Static (non-interactive) item kinds - a separator rule and a section-header caption.
#define ui_menu_separator() ui_menu_separator_impl()
#define ui_menu_header(label) ui_menu_header_impl(label)

// Runs the body (and ui_submenu_end) only while THIS submenu is expanded; a closed submenu still emits its parent row
// (inside begin) but skips the body. Nesting is safe even though the latch is a file static: each loop re-evaluates
// the latch from its own begin() at init and the for-update always pairs the matching end() - see ui_layout_area.
#define ui_submenu(label, ...) \
    for ( \
         UI_MENU_SUBMENU_LATCH = (ui_submenu_begin_impl(label, (FlSubmenuConfig) { __VA_ARGS__ }) ? 0 : 1); \
         UI_MENU_SUBMENU_LATCH < 1; \
         UI_MENU_SUBMENU_LATCH = 1, ui_submenu_end_impl())

#define ui_menu_region(region_id, menu_id, ...) \
    for ( \
         UI_MENU_REGION_LATCH = (ui_menu_region_begin_impl(region_id, menu_id, (FlMenuRegionConfig) { __VA_ARGS__ }), 0); \
         UI_MENU_REGION_LATCH < 1; \
         UI_MENU_REGION_LATCH = 1, ui_menu_region_end_impl())

// The strip body ALWAYS runs (tops are persistent), so ui_menubar latches like ui_layout_area, not
// like ui_menu. ui_menubar_menu runs its body only while THIS top's dropdown is open (like ui_submenu).
#define ui_menubar(bar_id, ...) \
    for ( \
         UI_MENUBAR_LATCH = (ui_menubar_begin_impl(bar_id, (FlMenuBarConfig) { __VA_ARGS__ }), 0); \
         UI_MENUBAR_LATCH < 1; \
         UI_MENUBAR_LATCH = 1, ui_menubar_end_impl())

#define ui_menubar_menu(label, ...) \
    for ( \
         UI_MENUBAR_MENU_LATCH = (ui_menubar_menu_begin_impl(label, (FlMenuBarMenuConfig) { __VA_ARGS__ }) ? 0 : 1); \
         UI_MENUBAR_MENU_LATCH < 1; \
         UI_MENUBAR_MENU_LATCH = 1, ui_menubar_menu_end_impl())
// clang-format on
