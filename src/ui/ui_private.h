#pragma once

#include <platform/input_types.h>
#include "background.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <core/types.h>
#include <flowi/ui/element_state.h> // @generated FlElementState (api_gen)
#include <flowi/ui/feedback.h>
#include <flowi/ui/icon.h> // @generated FlIcon / FlIcon_Count (api_gen)
#include <flowi/ui/key.h>  // @generated FlKey / FlKey_Count (api_gen)
#include <flowi/ui/layout.h>
#include <flowi/ui/popup.h> // @generated FlPopupConfig (api_gen)
#include <flowi/ui/theme.h>
#include "input_actions.h"
#include <ui/render_backend.h>
#include <ui/list_api.h>
#include <ui/list_uniform.h>
#include <ui/ui_types.h>
#include "type_ahead.h"
#include "ui.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define UI_INPUT_MAX_GAMEPADS 16

#define UI_INPUT_GAMEPAD_DEFAULT_DEAD_ZONE 0.15f

#define UI_MOUSE_DOUBLE_CLICK_TIME 0.3f
#define UI_MOUSE_DOUBLE_CLICK_MAX_DIST 6.0f
#define UI_MOUSE_STATIONARY_THRESHOLD 2.0f
// Movement (in logical px) the cursor must travel from the press point before an FlItem_DragThreshold
// item's drag latches - below this a press-and-release reads as a click, not a drag. Kept under
// UI_MOUSE_DOUBLE_CLICK_MAX_DIST so a double-click jiggle never trips a drag.
#define UI_MOUSE_DRAG_THRESHOLD 4.0f
#define UI_MOUSE_INVALID_POS -999999.0f

// Slider constants
#define UI_SLIDER_PADDING ((float)FlSpacing_SM)     // Horizontal padding for slider track (8px)
#define UI_SLIDER_THUMB_WIDTH ((float)FlSpacing_LG) // Width of slider thumb (24px, matches checkbox)
#define UI_SLIDER_TRACK_HEIGHT 8.0f                 // Thickness of the slider's filled/unfilled track bar
#define UI_SLIDER_LOG_EPSILON 0.001f                // Safety offset for logarithmic calculations
#define UI_SLIDER_ANALOG_SENSITIVITY 3.0f           // Gamepad analog stick sensitivity multiplier

// Max depth of the theme-color override stack (scoped subtree overrides; see FlUiCtx).
#define UI_THEME_OVERRIDE_MAX 16

// Overlay z bands. Every floating element flowi emits takes its z from here; equal z ties break on emission
// order (Clay's root sort swaps only on strictly-less, so it is stable). Internal only - no public FL_Z_*
// exists and a panel author never names a z number.
#define UI_Z_DOCK_CONTENT 0        // docked tiles, their tab strips, and docked panel bodies
#define UI_Z_OVERLAY 1000          // app/panel overlays: tooltip, menu, dropdown, modal backdrop, list drag chrome
#define UI_Z_DOCK_DRAG_CHROME 3000 // the dock drag ghost: its fill, its border, and the title it carries

struct VirtualKeyboard;
struct ScrollAreaStackEntry;
struct WidgetScrollInfo;
struct Painter;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct MouseState {
    float x;
    float y;
    float prev_x;
    float prev_y;
    float delta_x;
    float delta_y;
    float raw_delta_x; // Accumulated raw relative-motion delta this frame (hidden drag); cleared in
                       // fl_platform_input_update
    float raw_delta_y;
    float raw_delta_x_cached; // Cached per-frame raw delta read by ui_mouse_drag_delta (set before clearing)
    float raw_delta_y_cached;
    float wheel_x; // Accumulated wheel this frame (cleared at end)
    float wheel_y;
    float wheel_x_cached; // Cached for Clay (set before clearing)
    float wheel_y_cached;
    bool wheel_axis_swap_requested;
    bool buttons[FlMouseButton_Count];
    bool buttons_pressed[FlMouseButton_Count];
    bool buttons_released[FlMouseButton_Count];
    float button_hold_duration[FlMouseButton_Count];
    u64 button_press_timestamp[FlMouseButton_Count];
    float click_pos_x[FlMouseButton_Count];
    float click_pos_y[FlMouseButton_Count];
    float drag_max_distance_sqr[FlMouseButton_Count];
    u32 click_count[FlMouseButton_Count];
    u64 last_click_time[FlMouseButton_Count];
    float stationary_timer;
    bool is_inside_window;
    bool position_valid;
} MouseState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Cap for the typed text accumulated in one pump (KeyboardState.text_input_storage) and for the per-frame snapshot
// (ctx->text_input_buffer). Text past the cap is dropped, so the earliest characters of the frame win.
#define UI_TEXT_INPUT_THIS_FRAME_CAP 64

typedef struct KeyboardState {
    bool keys[FlKey_Count];
    bool keys_pressed[FlKey_Count];
    bool keys_released[FlKey_Count];
    float key_hold_duration[FlKey_Count];
    u32 modifiers;
    FlString text_input; // points into text_input_storage while non-empty
    char text_input_storage[UI_TEXT_INPUT_THIS_FRAME_CAP];
} KeyboardState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Navigation software repeat helper - interval-based timing: fires once when crossing each repeat interval
// boundary.

#include <math.h>

// Shared software-repeat timing for held keys (navigation and text editing).
#define UI_NAV_REPEAT_DELAY 0.25f // seconds held before continuous repeat starts
#define UI_NAV_REPEAT_RATE 0.05f  // seconds between repeats (20 per second)

static inline bool input_should_nav_repeat(const KeyboardState* kb, FlKey key, float delay, float rate, float dt) {
    if (!kb->keys[key] || kb->key_hold_duration[key] <= delay) {
        return false;
    }
    float time_since_delay = kb->key_hold_duration[key] - delay;
    return fmodf(time_since_delay, rate) < dt;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct GamepadState {
    bool connected;
    bool buttons[FlGamepadButton_Count];
    bool buttons_pressed[FlGamepadButton_Count];
    bool buttons_released[FlGamepadButton_Count];
    float button_hold_duration[FlGamepadButton_Count];
    u64 button_press_timestamp[FlGamepadButton_Count];
    float axes[FlGamepadAxis_Count];
    float axes_prev[FlGamepadAxis_Count];
    float dead_zone;
    u32 id;
} GamepadState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Cap on the files one frame's OS drops carry, and on the bytes their paths take in the stable
// per-frame snapshot. A drop past either cap is discarded with a warning.
#define UI_DROPPED_FILES_CAP 32
#define UI_DROPPED_PATH_BYTES 8192

typedef struct WindowEventState {
    bool focus_gained;
    bool focus_lost;
    bool resized;
    bool close_requested;

    // Files the OS dropped since the last update. The paths ride render_frame_allocator, so
    // fl_platform_input_update snapshots them into stable FlUiCtx storage before the frame body runs.
    u32 dropped_file_count;
    FlString dropped_files[UI_DROPPED_FILES_CAP];
    FlVec2 dropped_file_positions[UI_DROPPED_FILES_CAP];
} WindowEventState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlInputState {
    MouseState mouse;
    KeyboardState keyboard;
    GamepadState gamepads[UI_INPUT_MAX_GAMEPADS];
    WindowEventState window_events;

    bool window_focused;

    double time;
    double delta_time;

    bool quit_requested;
    bool text_input_active;

    void* user_data;
} FlInputState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Item flags for flag-based widget composition (FlItemFlags) are generated from item_flags.def into
// flowi/ui/item_flags.h (included via ui.h).
//
// One flag stays C-internal and is never crossed to Rust, so it is not part of the generated FlItemFlags
// mirror: the opt-in click-vs-drag disambiguation bit. When set, signal.dragging latches only after the cursor
// crosses UI_MOUSE_DRAG_THRESHOLD from the press point, so a press-and-release under the threshold reads as a
// click, never a spurious drag. Sliders/scrollbars omit it - they want the thumb to follow the cursor
// immediately. It lives above the generated bits (max scroll_target = 1 << 8).
enum {
    FlItem_DragThreshold = (1 << 9),
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ItemState {
    // Public widget state (returned by ui_get_element_state)
    FlElementState state;

    FlItemFlags flags;
    u64 frame;
    u64 hit_pass; // UiShared.hit_test_pass of the last hit-test pass that updated this item

    // The item's rect cut by the clip chain it was emitted under, as of the last hit-test pass (empty when
    // min > max).
    Vec2 hit_min;
    Vec2 hit_max;

    // Internal interaction state
    bool was_focused;         // Had focus last frame (for detecting transitions)
    bool auto_focus_consumed; // One-shot latch; reset when the item leaves persistent state

    // Slider/Drag specific
    Vec2 drag_start_mouse;
    bool is_dragging;

    // Double-click tracking (valid if FlItemFlags_double_click flag set)
    u32 click_count;
    f64 last_click_time;

    // Toggle animation (0.0 to 1.0): checkbox check/uncheck, and the switch knob's on/off slide.
    // Per-item, so a checkbox and a switch never share it.
    float check_anim_t;

    // Popup scope tracking
    u32 popup_scope_id;   // 0 = main UI, non-zero = belongs to popup
    bool in_active_popup; // True if rendered inside currently active popup (frame-local)

    // Last signal returned by ui_item_behavior (for test inspection)
    FlSignal last_signal;
} ItemState;

// Bounding-box corner accessors from an ItemState's laid-out rect.
#define ITEM_LEFT(item) ((item)->state.pos.x)
#define ITEM_TOP(item) ((item)->state.pos.y)
#define ITEM_RIGHT(item) ((item)->state.pos.x + (item)->state.size.x)
#define ITEM_BOTTOM(item) ((item)->state.pos.y + (item)->state.size.y)
#define ITEM_CENTER_X(item) ((item)->state.pos.x + (item)->state.size.x * 0.5f)
#define ITEM_CENTER_Y(item) ((item)->state.pos.y + (item)->state.size.y * 0.5f)

struct ImageCache;
struct ImageRegistry;
struct ScrollAreaState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu system
//
// The whole open menu chain lives in one popup focus scope with an internal open path. Per-level highlight is the
// single highlighted item at that level, driven by both hover and keyboard.

#define UI_MENU_MAX_DEPTH 8  // open-path capacity (fixed cap; overflow asserts in debug)
#define UI_MENU_MAX_ITEMS 64 // per-level keyboard-navigable item cap; items beyond it render but aren't nav targets

// Fold a menu row's build level into its per-level index to form the row's FlLayoutId hash seed: level in the
// high bits, index in the low 16, so every (level, index) pair maps to a distinct seed.
static inline i32 ui_menu_row_id_seed(int level, int index) {
    return (i32)(((u32)level << 16) | (u32)index);
}

// Cold-open hover-dwell (seconds): how long the pointer must rest on a CLOSED submenu parent before it opens.
// Accumulated per menu item.
#define UI_MENU_DWELL_SECONDS 0.30f
// Branch-switch grace (seconds): when the pointer moves onto a sibling while a submenu is already open, wait this
// long before collapsing+switching, so a diagonal move INTO the open submenu survives. Cancelled early when the
// cursor is moving away from the open submenu (a cheap dot-product, the poor-man's mouse-safe-triangle).
#define UI_MENU_BRANCH_GRACE_SECONDS 0.25f

// Per-item recorded-label cap for type-ahead; matches the prefix cap so a full-length prefix can still match.
#define UI_MENU_TYPEAHEAD_CAP 24
_Static_assert(UI_MENU_TYPEAHEAD_CAP <= TYPE_AHEAD_CAP, "menu prefix cap must fit TypeAheadState");

// Persistent per-level open-path state: the highlight and the expanded-child wiring that define which menu chain
// is open and where. This survives across frames (a keyboard selection sticks; a stationary hover must not
// re-derive it) and across a Detachable Panel promote/demote, so it lives in UiShared. The per-frame emission
// scratch a level records as items emit (counts, ids, navigability) lives separately in UiMenuLevelFrame.
typedef struct UiMenuLevel {
    i32 highlight_index;   // the single highlighted item at this level (-1 = none); shared by hover and keyboard
    bool init_highlight;   // pending "highlight the first navigable item" on open; consumed during emission
    u32 expanded_child_id; // widget id of the submenu parent expanded into the next level (0 = none → this is deepest)
    bool flip_left;        // this level opened to the LEFT of its parent; children inherit the bias
    // Vertical scroll offset (logical px) for an over-tall level whose natural item list exceeds the viewport: the
    // panel is height-capped and its contents scroll by this much (0 = top). Driven by the wheel over the level and by
    // keyboard scroll-into-view; clamped to [0, content - viewport] each frame via scroll_core. Reset to 0 whenever the
    // level (re)opens or the open path collapses through it, so a reopened level starts at the top.
    f32 scroll_offset;
} UiMenuLevel;

// Per-frame per-level emission scratch (per-window): the item set a level records as it emits this frame. Counts
// restart at 0 and the arrays re-fill every frame; the arrays are read at the NEXT begin to resolve ctx->hot_id
// and step the highlight.
typedef struct UiMenuLevelFrame {
    i32 item_count; // running count of items emitted at this level this frame (assigns item indices)
    // Per-item navigability recorded during emission, read at begin() to move the highlight skipping non-nav items
    // (disabled rows, separators and headers).
    bool item_navigable[UI_MENU_MAX_ITEMS];
    // Open-path identity + submenu wiring, recorded during emission and read back at the next begin():
    u32 item_id[UI_MENU_MAX_ITEMS];       // resolved widget id per emitted item (maps ctx->hot_id -> level + index)
    bool item_submenu[UI_MENU_MAX_ITEMS]; // whether the item is a submenu parent (a pure container) vs a leaf action
    // Type-ahead label prefix per item, recorded during emission (bounded copy - never a borrowed pointer that could
    // dangle a frame later) and matched at the next begin() exactly like item_navigable is stepped over. Non-navigable
    // rows still record a length but are skipped by the same navigability gate arrow-nav uses.
    char item_label[UI_MENU_MAX_ITEMS][UI_MENU_TYPEAHEAD_CAP];
    u8 item_label_len[UI_MENU_MAX_ITEMS]; // valid bytes in item_label[i] (<= UI_MENU_TYPEAHEAD_CAP)
    // Scroll geometry recorded during emission and read back at the next begin() (same cross-frame contract as the
    // arrays above): each row's top and height in content space (relative to the first row, padding excluded), and the
    // level's total natural content height. Used to decide the height cap, clamp the scroll offset, and scroll the
    // highlighted row into view. natural_height is reset to 0 as the first row of the level emits, so the value read at
    // begin() is the PREVIOUS frame's complete total (the panel-cap decision is one frame late, like flip/clamp).
    f32 item_top[UI_MENU_MAX_ITEMS];
    f32 item_height[UI_MENU_MAX_ITEMS];
    f32 natural_height;
} UiMenuLevelFrame;

// Persistent menu open-path identity: the live chain of what is open right now - which menu owns it, how
// deep it goes, and the dwell/arbitration state that carries from frame to frame. Keyed by clay/widget ids
// (owner_scope_id, per-level expanded_child_id), so it is part of the shared id-keyed widget-state store and survives
// a Detachable Panel promote/demote. Lives in UiShared (one open chain app-wide). The per-frame emission scaffolding
// the builder reuses each frame (the build cursor, item arrays) lives in UiMenuFrame on the per-window FlUiCtx.
typedef struct UiMenuState {
    bool active;        // a menu chain is open
    u32 owner_scope_id; // clay/scope id of the menu that owns the open chain (also the popup focus scope id)
    FlVec2 anchor;      // cursor point captured at open time (screen space); no live-follow
    // Frame the chain opened. Input handling compares frame_counter against it so the input that opened the chain
    // does not immediately dismiss or activate within it; the open transition itself writes chain state on this
    // frame by design.
    u64 open_frame;
    i32 depth; // number of open levels (0 = closed, 1 = root open, >1 = open submenus). active is the
               // open test; depth drives which level the keyboard navigates (always the deepest).
    // Last-input-wins arbitration: the highlight is shared by hover and keyboard, owned by whichever moved last.
    // hover_drives is set when the pointer moved this frame and cleared when the keyboard moved the highlight, so a
    // stationary pointer sitting over a row never yanks the highlight back from a keyboard selection.
    bool hover_drives;
    FlVec2 last_mouse_pos; // pointer position last frame, to detect movement for the arbitration above
    // Cold-open dwell + branch-switch grace. Only one menu item is hot at a time, so this single keyed accumulator
    // is per-item: it resets whenever the hot item changes. hover_accum serves both triggers (dwell vs grace,
    // chosen by whether a sibling is already open at that level).
    u32 dwell_item_id; // the hot menu item the timers are accumulating against (0 = none)
    float hover_accum; // seconds the pointer has rested on dwell_item_id (cold-open dwell, or branch-switch grace)
    // Type-ahead prefix - one accumulator for the single open chain app-wide, matched against the deepest open
    // level's recorded labels.
    TypeAheadState typeahead;
    UiMenuLevel levels[UI_MENU_MAX_DEPTH];
} UiMenuState;

// Per-frame menu emission scratch (per-window): the builder's cursor and the per-level item records it rebuilds each
// frame between begin/end. Only the open-chain identity (UiMenuState) is shared.
typedef struct UiMenuFrame {
    i32 build_level;      // level currently being emitted between begin/end (0 at the root, +1 inside each submenu)
    bool close_requested; // an activated item asked to close the chain (per-item close policy)
    i16 z_base;           // root panel z captured at begin; submenu panels stack at z_base + level so each is on top
    u32 menubar_scope; // scope id of the menu bar currently being built between ui_menubar_begin/end (0 = none); lets
                       // ui_menubar_menu resolve "is my bar the active chain" without re-threading the bar id
    // The menu bar's tops, recorded every frame whether or not the bar owns the chain; copied into levels[0] only
    // while the bar owns the chain.
    UiMenuLevelFrame bar_tops;
    UiMenuLevelFrame levels[UI_MENU_MAX_DEPTH];
} UiMenuFrame;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// App-level shared resource tier. One per app, brought up once by the runner and referenced by every
// per-window FlUiCtx through ctx->shared. Holds the resources that never move
// between windows: the render backend, font cache, image readiness registry, host-injected base theme,
// standard icons, and scaled bitmap fonts. Per-window frame state (input, focus, popups, the theme_override
// stack, DPI scale, etc.) stays on FlUiCtx.
typedef struct UiShared {
    FlRenderBackend renderer;
    struct FlFontCache* font_cache;
    struct ImageRegistry* image_registry;

    // Base theme (host-injected; owned by Frontend). Per-window scoped overrides layer over this in FlUiCtx.
    const FlThemeColors* theme_colors;

    // Toolkit-chrome icons (loaded from a VFS mount by fl_icon_load), indexed by FlIcon
    ImageHandle icons[FlIcon_Count];

    // Persistent id-keyed widget-state store, keyed by the location-independent clay hashed id
    // (ui_layout_id_to_clay_id has no parent/position component). This never moves between windows, so a Detachable
    // Panel keeps its element state, scroll offsets, and scroll containment across a promote/demote for free.
    hashmap(u32, ItemState) item_state;                      // per-widget element + interaction state
    hashmap(u32, struct ScrollAreaState) scroll_states;      // scroll offsets / content extents per scroll area
    hashmap(u32, struct WidgetScrollInfo) widget_scroll_map; // widget id -> scroll containment info

    // Persistent menu open-path identity (one open chain app-wide). The per-frame emission scaffolding stays on the
    // per-window FlUiCtx (UiMenuFrame); only the live "what is open" identity is shared.
    UiMenuState menu;

    // App UI feedback hook. One app-wide hook the interaction layer calls on discrete, human-driven
    // events (focus moved, nav edge hit, widget activated) so the app can attach sound/haptics/a11y/telemetry.
    // NULL by default: no event is emitted until an app installs one via fl_ui_set_feedback_hook.
    FlUiFeedbackCallback feedback_hook;
    void* feedback_user_data;

    // App-global docking state: the registry of dock workspaces (the
    // set of trees is app-global; each per-window FlUiCtx points at the one it hosts via ctx->dock_workspace), the
    // injected host seam for OS-window promotion, and the deferred promote queue + live promotion records. Opaque
    // here; created/destroyed with the rest of UiShared. See ui/dock/dock_shared.h.
    struct FlDockShared* dock;

    // App-global localization: the loaded string table SID_ display text resolves against. Null until
    // fl_localization_init brings it up; a null one leaves every string rendering exactly as given.
    struct Localization* localization;

    // Retirement epoch for item_state. Every window stamps the records it refreshes with the current epoch; the
    // records left unstamped are retired once the whole pass has run, then the epoch advances. An FlApp owns the
    // pass boundary (ui_app_frame_begin/_end) so one window's cleanup never drops another live window's records;
    // standalone callers leave item_epoch_app_owned false and ui_update_end retires per context.
    u64 item_epoch;
    u64 hit_test_pass; // Incremented once per ui_hit_test_frame; ItemState.hit_pass tags the pass that updated it
    bool item_epoch_app_owned;

#ifndef NDEBUG
    // Debug: per-frame duplicate-id tracker. App-global: with each window's subtree drawn into the one
    // shared id space, uniqueness is an app-wide invariant. Maps Clay element id -> dummy ItemState (presence check
    // only).
    hashmap(u32, ItemState) debug_id_tracker;

    // Debug: who owns clearing debug_id_tracker. The tracker spans every window, so it is cleared once per full UI
    // pass, before any window emits. An FlApp driving the windows clears it at the tick boundary
    // (ui_app_frame_begin) and sets this, which stops the per-window ui_update_begin from clearing it too.
    // Standalone callers and tests driving a single context leave it false, so ui_update_begin keeps that clear.
    bool debug_id_tracker_app_owned;
#endif
} UiShared;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlUiCtx {
    FlArena* persistant;
    FlArena* render_frame_allocator;
    FlArena* render_frame_allocator_state;

    UiShared* shared; // App-level shared resource tier (renderer, fonts, images, base theme, icons); see UiShared

    // Per-window layout context. A Clay context holds exactly one layout tree, so each window owns its own,
    // captured from Clay_Initialize in ui_create. ui_make_current selects it via Clay_SetCurrentContext
    // alongside g_ui_ctx before this window emits its frame.
    struct Clay_Context* clay_ctx;

    // Per-window render target: the backend-owned surface this context renders
    // into. The host creates one per window via the render backend and registers it with ui_set_surface; the
    // surface-scoped seams (render / change_resolution) take it. Null until the host sets it - a context with no
    // surface simply does not render.
    FlSurface* surface;

    // The dock workspace this window hosts. NON-OWNING: the registry in ctx->shared->dock owns
    // every workspace; this is just which one dock_workspace_current() resolves to for this window. Defaults to
    // the shared registry's entry 0 (the main workspace) at ui_create; a promoted window is rebound to its own
    // workspace at materialization (ui_ctx_set_dock_workspace). Null only for a context with no shared dock state.
    struct FlDockWorkspace* dock_workspace;

    FlInputState input_state;
    ActionSystemState* action_state;
    // Persistent id-keyed widget state (item_state, scroll_states, widget_scroll_map) lives in the shared tier so
    // it survives a Detachable Panel promote/demote; reach it through ctx->shared->.
    FlBackgroundController background; // Backend-agnostic background state (crossfade / alpha / clear)
    u64 frame_counter;
    float delta_time;
    struct Painter* painter; // Painter API for custom drawing
    FlFontId font_styles[FontStyle_Count];
    FlFontId active_font;

    // Theme support - widgets read colors through the resolution seam
    // (ui_get_theme_colors), never the base pointer directly. Resolution walks a
    // small override stack layered over the shared base theme (ctx->shared->theme_colors).
    // With the stack empty, resolution falls through to the base theme; ui_theme_push/ui_theme_pop
    // layer scoped subtree overrides on top. The override stack is per-window - each stack entry points
    // at owned merged storage so it stays valid for the subtree's lifetime.
    const FlThemeColors* theme_override_stack[UI_THEME_OVERRIDE_MAX]; // top = depth-1
    FlThemeColors theme_override_storage[UI_THEME_OVERRIDE_MAX];      // merged layers backing the stack
    int theme_override_depth;                                         // 0 = resolve to the base theme

    // Layout dimensions (for visibility checks)
    float layout_width;
    float layout_height;

    // UI scale - two absolute factors the core is handed via ui_set_scale. Authored logical pixels are
    // multiplied by ui_scale (chrome) / text_scale (type). Default (1.0, 1.0).
    float ui_scale;
    float text_scale;

    // IMGUI interaction tracking
    u32 hot_id;      // ID of widget mouse is over
    u32 active_id;   // ID of widget being interacted with
    u32 next_hot_id; // Hot ID for next frame (built during frame)

    // Mouse state captured at ui_update_begin (before input clears the flags)
    bool mouse_pressed_this_frame;
    bool mouse_released_this_frame;
    // Per-button edge snapshot taken at the same point; ui_mouse_pressed / ui_mouse_released read these,
    // ui_mouse_down reads the buttons[] level directly.
    bool mouse_buttons_pressed_this_frame[FlMouseButton_Count];
    bool mouse_buttons_released_this_frame[FlMouseButton_Count];
    // Per-key edge snapshot taken at the same point; ui_key_pressed / ui_key_released read these, ui_key_down
    // reads the keys[] level directly.
    bool keys_pressed_this_frame[FlKey_Count];
    bool keys_released_this_frame[FlKey_Count];
    // Per-pad button edge snapshot taken at the same point.
    bool gamepad_buttons_pressed_this_frame[UI_INPUT_MAX_GAMEPADS][FlGamepadButton_Count];
    bool gamepad_buttons_released_this_frame[UI_INPUT_MAX_GAMEPADS][FlGamepadButton_Count];

    // This-frame typed text, snapshotted before fl_platform_input_update clears the raw kb fields. Consumers
    // (ui_text_input, dropdown search, list type-ahead) read it.
    FlString text_input_this_frame;
    char text_input_buffer[UI_TEXT_INPUT_THIS_FRAME_CAP];
    // Set by a text input that holds keyboard focus while the frame body runs; ui_update_end publishes it as
    // input_state.text_input_active.
    bool text_input_focused_this_frame;

    // This-frame dropped-file snapshot, taken at the same point as the typed text; the dropped-file queries read
    // these.
    u32 dropped_file_count_this_frame;
    FlString dropped_files_this_frame[UI_DROPPED_FILES_CAP];
    FlVec2 dropped_file_positions_this_frame[UI_DROPPED_FILES_CAP];
    char dropped_file_buffer[UI_DROPPED_PATH_BYTES];

    // Cursor request. requested_cursor is per-frame last-writer-wins: ui_update_begin resets it to the arrow,
    // fl_cursor_set overwrites it during the frame, and the app flushes the resolved
    // value to the input backend after the frame. cursor_visible is a sticky toggle (not reset per frame).
    FlCursor requested_cursor;
    bool cursor_visible;

    // Hidden drag - the unbounded relative scrub a knob/fader/numeric-drag-field rides.
    // hidden_drag_active gates ui_mouse_drag_delta; hidden_drag_scale is the per-call sensitivity
    // the raw delta is multiplied by; hidden_drag_press_{x,y} is the press point begin grabbed, the warp-back
    // target on end. The platform mechanics are recorded here and flushed by the app after the frame, mirroring
    // the cursor request: relative_mouse_mode is a sticky toggle on for the whole drag; warp_pending is a one-shot
    // set on end and consumed (warp_{x,y}) by the app's flush.
    bool hidden_drag_active;
    float hidden_drag_scale;
    float hidden_drag_press_x;
    float hidden_drag_press_y;
    bool relative_mouse_mode;
    bool warp_pending;
    float warp_x;
    float warp_y;

    // Focus management
    u32 focused_id;      // Currently focused widget
    u32 prev_focused_id; // Focus from last frame (for persistence)
    u32 last_focused_id; // Last widget that had focus (for restoration)

    // Tab order (built each frame in frame arena)
    u32* focus_order;         // Array of focusable widget IDs in declaration order
    u32 focus_order_count;    // Number of focusable widgets this frame
    u32 focus_order_capacity; // Allocated capacity of focus_order array

    // Navigation input (captured like mouse)
    bool nav_next_pressed;     // Tab or Gamepad RB
    bool nav_prev_pressed;     // Shift+Tab or Gamepad LB
    bool nav_activate_pressed; // Enter/Space or Gamepad A

    // Navigation directional state (shared struct for frontend use)
    FlNavFrame nav_frame;

    // Auto-scroll support
    bool should_scroll_to_focused; // Track if focus changed via navigation (keyboard/gamepad) this frame

    // Scroll area stack (for tracking widget containment). The per-frame emission stack stays per-window; the
    // resolved widget->scroll-area map it feeds is persistent and lives in ctx->shared->widget_scroll_map.
    struct ScrollAreaStackEntry* scroll_area_stack; // Stack of active scroll areas during rendering
    u32 scroll_area_stack_depth;                    // Current depth in scroll area stack
    u32 scroll_area_overflow;                       // Open scroll areas nested past the stack's capacity

    // Popup system
    u32 popup_stack_count; // ui_popup scopes begun since the last popup close

    // Focus scope management
    u32 popup_focus_scope_id;       // Non-zero = popup owns focus
    u32 current_popup_scope_id;     // Scope ID being rendered (set during popup render)
    u32 saved_focused_id;           // Focused widget before popup opened
    u64 popup_open_frame;           // Frame when popup first opened (for click-outside handling)
    bool popup_needs_initial_focus; // Flag to auto-focus first widget in popup
    bool popup_auto_close;          // Whether current popup should auto-close on click-outside
    bool popup_closed_this_frame;   // True if popup was closed this frame (for dropdown sync)

    // True between ui_update_begin and ui_update_end - the window in which render_frame_allocator holds this
    // frame's storage. fl_frame_str asserts on it, so a host that drives the UI layer directly (no fl_app window
    // loop) satisfies the same precondition an fl_app frame callback does.
    bool frame_open;

    // Modal tracking (separate from popup stack)
    u32 modal_scope_id; // Non-zero = modal dialog is active (prevents popup cleanup)

    // Menu system - per-frame emission scaffolding for the context-menu chain. The persistent open-path
    // identity (what is open, the highlight) lives in ctx->shared->menu.
    UiMenuFrame menu_frame;

    // Rendering state
    bool rendering_popup; // True during popup render pass
    u8 global_alpha;      // Global alpha for fade transitions (0-255, default 255)

    // Virtual keyboard state
    bool physical_keyboard_available;                // True if physical keyboard detected (default: true)
    struct VirtualKeyboard* active_virtual_keyboard; // Non-null when virtual keyboard is active
    FlTextInputState* vk_target_text_input;          // Text input being edited by virtual keyboard
    FlArena* vk_arena; // Dedicated arena for the active keyboard; rewound each focus-gain so the
                       // keyboard + its title/allowed_chars/text copies never leak the persistent arena

    // List Uniform state
    FlListUniformContext* list_uniform_ctx; // List uniform context (created in ui_create)

    // List API state
    FlListContext* ui_list_ctx; // List context (created in ui_create)

#ifdef FLOWI_UI_INSPECTOR
    // This window's last-frame render commands, captured at ui_update_end for test introspection
    // (ui_get_render_command_count / ui_get_render_command). Per-window output, so it lives on the context.
    //
    // The capture OWNS everything it exposes: an element's name and text come from caller memory that typically
    // lives in a per-frame arena rewound at the next ui_update_begin, while the readers run between frames - so
    // the capture copies the strings. Both the array and the copied bytes come from render_command_arena, which
    // is rewound at the start of each capture.
    struct UiRenderCommand* last_render_commands;
    i32 last_render_command_count;
    FlArena* render_command_arena;
#endif
} FlUiCtx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Bring up the app-level shared resource tier once: the host constructs the render backend (with
// its surface binding) and injects it here; the core never names a concrete backend. The backend's vtable is
// copied into the shared tier, which then backs every per-window FlUiCtx.
UiShared* ui_shared_create(struct FlArena* arena, FlRenderBackend* backend);
void ui_shared_destroy(UiShared* shared);

// Construct a per-window context attached to an already-created shared tier. The runner brings up the shared
// tier once via ui_shared_create and hands it to each window's ui_create.
FlUiCtx* ui_create(struct FlArena* arena, UiShared* shared);
void ui_destroy(FlUiCtx* ctx);

// Frees every viewport3d table slot last begun by ctx; ui_destroy calls it.
void viewport3d_release_owner(const FlUiCtx* ctx);
FlUiCtx* ui_get_ctx(void);

// Bind this window's hosted dock workspace: a NON-OWNING pointer into the shared registry that
// dock_workspace_current() resolves to for this context. The runner calls it when materializing a promoted
// window so that window renders its own workspace; a plain window keeps the registry default set at ui_create.
void ui_ctx_set_dock_workspace(FlUiCtx* ctx, struct FlDockWorkspace* ws);

// Select ctx as the active window context: swaps both the core singleton (g_ui_ctx) and Clay's current
// layout context (ctx->clay_ctx). The runner calls this before each window emits its frame so layout, input, and
// render commands all land on that window's tree.
void ui_make_current(FlUiCtx* ctx);

// The single active UI context, defined in ui.c. ui_get_ctx() is the thin accessor over it.
extern FlUiCtx* g_ui_ctx;

// Register this window's backend-owned render surface. The host creates the
// surface from the render backend and hands it here; the surface-scoped render / change_resolution seams use
// it. Until set, the context renders nothing.
void ui_set_surface(FlUiCtx* ctx, FlSurface* surface);

void ui_set_font_cache(FlUiCtx* ctx, struct FlFontCache* font_cache);
void ui_set_theme(FlUiCtx* ctx, const FlThemeColors* colors);
// Standard toolkit icons load via the public fl_icon_load (declared in <flowi/ui/icon.h>).
void ui_theme_update(const FlThemeColors* colors);
const FlThemeColors* ui_get_theme_colors(FlUiCtx* ctx);
void ui_theme_push(FlUiCtx* ctx, FlThemeOverride override); // layer a scoped override over the resolved theme
void ui_theme_pop(FlUiCtx* ctx);                            // revert the top override
// Emit a semantic UI feedback event to the app-installed hook; a no-op when none is installed.
static inline void ui_emit_feedback(FlUiCtx* self, FlUiFeedbackKind kind, u32 from, u32 to, FlUiFeedbackDir dir) {
    FlUiFeedbackCallback cb = self->shared->feedback_hook;
    if (cb == nullptr) {
        return;
    }
    const FlUiFeedbackEvent event = { .kind = kind, .from = from, .to = to, .dir = dir };
    cb(&event, self->shared->feedback_user_data);
}

void ui_update_begin(FlUiCtx* self);
void ui_update_end(FlUiCtx* self, float dt);

// Begin one app-level UI pass. An FlApp calls this once per tick, before any window emits, to
// clear the app-global duplicate-id tracker at the one boundary that spans every window - and to mark the tracker
// app-owned so the per-window ui_update_begin stops clearing it. No-op in release builds (no tracker). self may
// be any window's context; the tracker lives in the shared tier they all share.
void ui_app_frame_begin(FlUiCtx* self);

// End the app-level UI pass opened by ui_app_frame_begin: retire the shared item-state records no window refreshed
// this pass and hand retirement back to ui_update_end. self may be any window's context.
void ui_app_frame_end(FlUiCtx* self);

// Virtual keyboard management
void ui_set_physical_keyboard_available(FlUiCtx* ctx, bool available);
void ui_virtual_keyboard_update_and_render(FlUiCtx* ctx);
void ui_virtual_keyboard_finish_if_inactive(FlUiCtx* ctx);

// Performs: ui_update_end -> input_update -> ui_update_begin
void ui_frame_end_and_update_input(FlUiCtx* ctx, double dt);

// ui_item_behavior - the flag-composed widget-behavior primitive - is declared in ui.h.

// Format a slider's display value into arena. Int sliders apply the format to the rounded integer value (so
// arbitrary int formats like "%3d" or "%d ms" are well-defined and a NULL format falls back to "%d"); float
// sliders apply it to the float value (NULL falls back to "%.2f").
FlString ui_slider_format_value(FlArena* arena, const char* format, float value, bool is_int);

// ui_layout_id_to_clay_id - resolve a LayoutId to its u32 element id - is declared in ui.h.

#ifdef FLOWI_UI_INSPECTOR
// Helper: Look up Clay element string name by ID (wraps Clay internal hashmap)
FlString ui_get_clay_element_name(u32 element_id);

// Snapshot the frame's Clay output into the context's capture; called from ui_update_end.
struct Clay_RenderCommandArray;
void ui_capture_render_commands(FlUiCtx* self, struct Clay_RenderCommandArray* commands);
#endif

#ifdef FLOWI_UI_INSPECTOR
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render command snapshot - Clay-independent types for MCP introspection

typedef enum UiRenderCommandType {
    UiRenderCmd_None,
    UiRenderCmd_Rectangle,
    UiRenderCmd_Border,
    UiRenderCmd_Text,
    UiRenderCmd_Image,
    UiRenderCmd_ScissorStart,
    UiRenderCmd_ScissorEnd,
    UiRenderCmd_Custom,
} UiRenderCommandType;

typedef struct UiColor4 {
    f32 r, g, b, a;
} UiColor4;

typedef struct UiRenderCommand {
    u32 id;
    i16 z_index;
    UiRenderCommandType type;
    f32 x, y, width, height;
    FlString name;

    // Rectangle / Image
    UiColor4 bg_color;
    FlCornerRadius corner_radius;

    // Text
    FlString text;
    UiColor4 text_color;
    u16 font_id;
    u16 font_size;

    // Border
    UiColor4 border_color;
} UiRenderCommand;

i32 ui_get_render_command_count(void);
bool ui_get_render_command(i32 index, UiRenderCommand* out);

#endif // FLOWI_UI_INSPECTOR
