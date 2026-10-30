#pragma once

#include <platform/input_types.h>
#include <core/arena.h>
#include <core/types.h>
#include <flowi/ui/cursor.h>        // @generated FlCursor / FlCursorShape (api_gen)
#include <flowi/ui/element_state.h> // @generated FlElementState (api_gen)
#include <flowi/ui/feedback.h>
#include <flowi/ui/image.h>      // @generated FlImageOpts (api_gen)
#include <flowi/ui/item_flags.h> // @generated FlItemFlags (api_gen)
#include <flowi/ui/key.h>        // @generated FlKey (api_gen)
#include <flowi/ui/nav.h>
#include <flowi/ui/signal.h>        // @generated FlSignal (api_gen)
#include <flowi/ui/text_config.h>   // @generated FlTextConfig (api_gen)
#include <flowi/ui/theme.h>         // @generated FlThemeColors (api_gen)
#include <flowi/ui/widget_config.h> // @generated FlWidgetConfig / FlSliderConfig / ... (api_gen)
#include "image.h"
#include "layout.h"
#include <ui/ui_types.h>
#include "ui_ids.h"
#include "ui_tokens.h"
#include "ui_variant_helpers.h"
#include <vfs/vfs.h>

// Current frame delta time in seconds (for frame-rate-independent animation).
float ui_get_delta_time(void);

// Current screen/viewport size in logical pixels. Returns {0, 0} before ui_create / outside an active context.
FlVec2 ui_get_screen_size(void);

#define FontStyle_Default FlFontStyle_Default
#define FontStyle_Bold FlFontStyle_Bold
#define FontStyle_Thin FlFontStyle_Thin
#define FontStyle_Light FlFontStyle_Light
#define FontStyle_Count FlFontStyle_Count

// Frame-lifetime string formatting is the generated Format api (flowi/ui/format.h, body in ui/ui.c):
//   ui_button(fl_format_frame_string("Hello %s", "hello"));

FlSignal ui_button_impl(FlString string, FlWidgetConfig cfg);
#define ui_button(text, ...) ui_button_impl(text, (FlWidgetConfig) { __VA_ARGS__ })

FlSignal ui_button_with_image_impl(FlString text, ImageHandle image, FlWidgetConfig cfg);
#define ui_button_with_image(text, image, ...) ui_button_with_image_impl(text, image, (FlWidgetConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_image_impl(ImageHandle handle, FlImageOpts opts);
#define ui_image(handle, ...) ui_image_impl(handle, (FlImageOpts) { __VA_ARGS__ });

void ui_dynamic_image_impl(FlDynamicImage* image, FlImageOpts opts);
#define ui_dynamic_image(image, ...) ui_dynamic_image_impl(image, (FlImageOpts) { __VA_ARGS__ })

// fl_icon_get / fl_icon_load are public fl_* symbols defined in ui.c against the generated
// <flowi/ui/icon.h> prototypes. FlIcon is the toolkit-chrome icon set; index shared->icons[] with
// FlIcon_* directly.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_text(FlString text);

void ui_text_styled_impl(FlString text, FlTextConfig config);
#define ui_text_styled(text, ...) \
    ui_text_styled_impl(text, (FlTextConfig) { .valign = FlTextVAlign_Center, __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Load a font at default_size (authored px; 0 selects a built-in default). The handle keeps that authored
// size - the current text scale is applied per draw, not baked here, so the font follows a later
// ui_set_scale. Per-size glyph atlases are realized lazily on first use.
FlFontId ui_font_load(FlVfsMount* mount, FlString relative_path, u16 default_size);

// fl_font_load_default / fl_font_load_fallback / fl_font_style_register / fl_font_style_set are public
// fl_* symbols defined in ui.c against the generated <flowi/ui/font.h> prototypes.

void ui_font_set(FlFontId font);

void ui_font_wait_load(FlFontId font_id);
bool ui_font_is_loaded(FlFontId id);
// Opt-in load barrier: pump I/O until every kicked-off font has its metrics ready (or failed), so setup can
// load several fonts and sync once before the first rendered frame. Returns false if any font failed.
bool ui_font_ensure_loaded(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_theme_update(const FlThemeColors* colors);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlSignal ui_widget_behavior(FlLayoutId id, FlWidgetConfig cfg);

// Generic flag-composed widget behavior - the primitive behind custom interactive widgets. flags selects
// which features are active (FlItemFlags_focusable, FlItemFlags_clickable, ...; see FlItemFlags in
// <flowi/ui/item_flags.h>). Returns the interaction signal (clicked/hovered/focused/activated).
//
// The flag decides focus; cfg.focusable overrides it per instance. FlFocusable_No subtracts the bit,
// leaving the widget out of focus_order, unreachable by tab and spatial nav, never auto-restored onto, and
// without a focus border. FlFocusable_Default (what a zeroed config carries) leaves the flag untouched, so a
// caller that never named the field keeps its family's behaviour. FlFocusable_Yes is not honoured here - a
// family that is not focusable by default (the scrollbar) adds the bit itself.
FlSignal ui_item_behavior(FlLayoutId id, FlItemFlags flags, FlWidgetConfig cfg);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checkbox widget

// Checkbox with label - returns signal (clicked/hovered/focused/activated)
FlSignal ui_checkbox_impl(FlString label, bool* checked, FlCheckboxConfig cfg);
#define ui_checkbox(label, checked, ...) \
    ui_checkbox_impl(label, checked, (FlCheckboxConfig) { .label_on_left = false, __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Switch widget

// Switch with label - boolean toggle drawn as a pill track with a knob that slides across as it toggles.
// Same boolean role as a checkbox (shares its click/activate behavior). Returns signal
// (clicked/hovered/focused/activated).
FlSignal ui_switch_impl(FlString label, bool* value, FlSwitchConfig cfg);
#define ui_switch(label, value, ...) \
    ui_switch_impl(label, value, (FlSwitchConfig) { .label_on_left = false, __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Radio group widget

// Radio group - exclusive single-choice selection across options[]. Selecting one option writes its index back to
// *selected_index, clearing the others. Returns a signal whose clicked is set on the frame selection changes.
FlSignal ui_radio_group_impl(FlString* options, int option_count, int* selected_index, FlRadioConfig cfg);
#define ui_radio_group(options, option_count, selected_index, ...) \
    ui_radio_group_impl(options, option_count, selected_index, (FlRadioConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Progress bar widget

// Progress bar - determinate, non-interactive. Paints a track + a fill sized to fraction (normalized [0,1],
// clamped). Pure paint: no id, no interaction, no animation.
void ui_progress_bar_impl(float fraction, FlProgressConfig cfg);
#define ui_progress_bar(fraction, ...) ui_progress_bar_impl(fraction, (FlProgressConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slider widget

// Float slider - returns signal (clicked/hovered/focused/activated)
FlSignal ui_slider_float_impl(FlString label, float* value, float min, float max, FlSliderConfig cfg);
#define ui_slider_float(label, value, min, max, ...) \
    ui_slider_float_impl(label, value, min, max, (FlSliderConfig) { .format = "%.2f", .step = 0.0f, __VA_ARGS__ })

// Integer slider - returns signal (clicked/hovered/focused/activated)
FlSignal ui_slider_int_impl(FlString label, int* value, int min, int max, FlSliderConfig cfg);
#define ui_slider_int(label, value, min, max, ...) \
    ui_slider_int_impl(label, value, min, max, (FlSliderConfig) { .format = "%d", .step = 1.0f, __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pill widget - pill-shaped toggle button for category filters
// Uses checkbox behavior with pill styling (FlRadius_Full)
// Selected = Solid variant, Unselected = Ghost variant

FlSignal ui_pill_impl(FlString label, bool* selected, FlWidgetConfig cfg);
#define ui_pill(label, selected, ...) ui_pill_impl(label, selected, (FlWidgetConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Chip widget - small tag/badge with soft variant

FlSignal ui_chip_impl(FlString label, bool* selected, FlWidgetConfig cfg);
#define ui_chip(label, selected, ...) ui_chip_impl(label, selected, (FlWidgetConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dropdown widget (uses popup system)

// Dropdown - interactive widget for selecting from a list of options. is_open is caller-owned popup state,
// selected_index is the caller-owned current option.
void ui_dropdown_impl(FlString label, bool* is_open, FlString* options, int option_count, int* selected_index,
                      FlDropdownConfig cfg);
#define ui_dropdown(label, is_open, options, option_count, selected_index, ...) \
    ui_dropdown_impl(label, is_open, options, option_count, selected_index,     \
                     (FlDropdownConfig) { .show_cancel = true, .min_width = 200.0f, __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tab bar widget - horizontal or vertical tab navigation. selected_index is in/out; returns true if the
// selection changed this frame. Every geometry field in the config is 0 = keep the built-in default; asking
// for strip padding or radius turns the bare layout row into a surface-colored pill that hugs its tabs.
bool ui_tab_bar_impl(FlString* tabs, int tab_count, int* selected_index, FlTabBarConfig cfg);
#define ui_tab_bar(tabs, tab_count, selected_index, ...) \
    ui_tab_bar_impl(tabs, tab_count, selected_index, (FlTabBarConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Link widget - clickable text with underline for hyperlinks. Returns a signal with clicked/focused state;
// focus adds a rounded border around the underlined text.
FlSignal ui_link_impl(FlString text, FlLinkConfig cfg);
#define ui_link(text, ...) ui_link_impl(text, (FlLinkConfig) { __VA_ARGS__ })

// fl_set_focus(FlLayoutId) is the public focus-set surface, in <flowi/ui/nav.h>.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input helpers

// Returns the mouse wheel delta for this frame
// Positive Y = wheel scrolled up, Negative Y = wheel scrolled down
FlVec2 ui_get_mouse_wheel(void);

// fl_nav_up_pressed / fl_nav_down_pressed and fl_consume_nav_* are the public nav surface, in
// <flowi/ui/nav.h>.

// Returns true if the specified key was just pressed this frame
bool ui_key_pressed(FlKey key);

// Returns true if the specified key is currently held down
bool ui_key_down(FlKey key);

// Returns true if the specified key was just released this frame
bool ui_key_released(FlKey key);

// Returns true if a text input had keyboard focus in the frame that ended last
bool ui_text_input_active(void);

// Returns the current keyboard modifier state (FlKeyModifier flags)
u32 ui_keyboard_modifiers(void);
u32 ui_dropped_file_count(void);
FlString ui_dropped_file(u32 index);
FlVec2 ui_dropped_file_position(u32 index);

// Mouse-button state, global and unrouted (mirrors ui_key_pressed/down/released for the keyboard).
// Every reader sees the same state regardless of what is under the cursor - buttons are never zeroed or
// re-routed mid-frame, so a single-frame read is always reliable (unlike the wheel, which is scoped to a
// Scroll Target). An owned interaction (a drag) is latched by the app: note the press over its region, then
// follow ui_mouse_down + ui_mouse_position until the release, even as the cursor leaves the region.

// Returns true if the mouse button was just pressed this frame (went down)
bool ui_mouse_pressed(FlMouseButton button);

// Returns true if the mouse button is currently held down
bool ui_mouse_down(FlMouseButton button);

// Returns true if the mouse button was just released this frame (went up)
bool ui_mouse_released(FlMouseButton button);

// This frame's position-derived pointer delta, in window pixels. The bounded companion to
// ui_mouse_drag_delta: it stops accumulating at the screen edge, which is what a consumer that mirrors the
// pointer (a software cursor, a hover readout) wants.
FlVec2 ui_mouse_delta(void);

// Per-slot gamepad state for a consumer that forwards raw pad input somewhere else (a remote session,
// a recorder) rather than reacting to the semantic nav actions. Slot is the 0-based player slot; an
// out-of-range or empty slot reads as disconnected with no buttons held, no edges, and every axis at 0.
bool ui_gamepad_connected(u32 slot);
bool ui_gamepad_button_down(u32 slot, FlGamepadButton button);
bool ui_gamepad_button_pressed(u32 slot, FlGamepadButton button);
bool ui_gamepad_button_released(u32 slot, FlGamepadButton button);
f32 ui_gamepad_button_hold_duration(u32 slot, FlGamepadButton button);
f32 ui_gamepad_axis(u32 slot, FlGamepadAxis axis);

// Cursor shape + visibility (the platform input seam, not the layout core's geometry) is the generated
// Cursor api: the `fl_cursor_*` prototypes are in flowi/ui/cursor.h, the bodies live in ui/ui.c:
//   fl_cursor_set(cursor)            request a per-frame shape (last-writer-wins, reset to arrow)
//   fl_cursor_get_requested()        the shape resolved for this frame (app flushes it to the backend)
//   fl_cursor_set_visible(visible)   sticky visibility toggle (not reset per frame)
//   fl_cursor_get_visible()          the current sticky visibility request

// Hidden relative drag - the unbounded, cursor-relative scrub a knob/fader/numeric-drag-field/timeline-pan
// rides. The app owns the latch: press→begin, held→read-delta, release→end.
//
// ui_mouse_hidden_drag_begin enters relative/unbounded mouse mode (the platform hides the OS cursor while it is
// on) and remembers the current mouse position as the press point. scale is the caller's sensitivity factor;
// ui_mouse_drag_delta multiplies the raw per-frame delta by it. Call inside a frame callback.
void ui_mouse_hidden_drag_begin(float scale);

// End the hidden drag: exit relative mode and warp the OS cursor back to the press point recorded by begin (the
// platform shows the cursor again). Call inside a frame callback.
void ui_mouse_hidden_drag_end(void);

// This frame's raw relative-motion delta multiplied by the scale passed to begin. Zero when no hidden drag is
// active. This is the unbounded scrub value - it never clamps at the screen edge (unlike a position-derived delta).
FlVec2 ui_mouse_drag_delta(void);

// App-flush helpers (mirroring fl_cursor_get_requested / fl_cursor_get_visible): the app pushes the resolved
// hidden-drag mechanics to the input backend after each frame. ui_get_relative_mouse_mode is the sticky
// relative-mode toggle (on for the whole drag). ui_take_pending_warp returns true and writes the warp-back target
// once after an end, consuming the one-shot request so it fires exactly one warp_mouse call.
bool ui_get_relative_mouse_mode(void);
bool ui_take_pending_warp(float* out_x, float* out_y);

// fl_action_pressed(FlAction) is the public context-free action query, in <flowi/ui/nav.h>.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Add empty space with fixed height (useful for list clipper padding in vertical layouts)
void ui_spacer(float height);

// Add empty space with fixed width (useful for virtualization spacers in horizontal layouts)
void ui_spacer_h(float width);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Element state

// Get an element's state including visibility, interaction state, and bounding box. Keyed by an element id, so it
// serves any laid-out element (a plain area as readily as a widget); rect is physical px (the Rust binding reports
// logical). Returns FlElementState with found=false if the element isn't in this frame's state.
FlElementState ui_get_element_state(FlLayoutId layout_id);

// Same as ui_get_element_state but keyed by the already-hashed clay element id (as returned by
// ui_layout_id_to_clay_id), for callers that only hold the raw id.
FlElementState ui_get_element_state_by_id(u32 element_id);

// Register an element's id in item_state so item_state_update fills its geometry each frame, readable via
// ui_get_element_state_by_id. Deliberately omits the scroll_target flag ui_scroll_target sets (which would
// reroute wheel input). Note: any item_state entry also enters hot/active arbitration - a tracked container can
// claim hover/press on its empty background (children still win via last-write-wins render order); it never
// claims the wheel without the flag.
void ui_track_element(FlLayoutId layout_id);

// Resolve a LayoutId (name + seed) to the opaque u32 element id the core keys its state under - Clay__HashString
// of the id. The hash is one-way. The in-tree key into item_state; surfaced to the binding as
// LayoutId::hashed_id().
u32 ui_layout_id_to_clay_id(FlLayoutId layout_id);

// Get the single topmost-hovered element id for this frame - the core's hot_id, the one element the cursor is
// over after this frame's hit-test arbitration (consume-takes-all, the front element wins). Returns 0 when
// nothing is hovered. The pull complement to reading each element's own state by id; surfaced to the binding as
// hot_element().
u32 ui_hot_id(void);

// Pointer position relative to a layout element's rect, in element-local logical (authoring) coordinates -
// the same space the Painter (ui/painter.h) authors in, so a canvas can hit-test the cursor against its own
// painted geometry directly. Computed as (mouse - element_origin) / ui_scale, so it stays the same logical
// position at any UI scale (unlike FlSignal.mouse_pos, which is element-local but in physical pixels). The
// element's resolved rect is from the previous frame (immediate mode), so query an element that existed last
// frame. Returns {0, 0} if the element is not in the layout (see ui_get_element_state().found).
FlVec2 ui_mouse_position(FlLayoutId layout_id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wheel input - Scroll Target routing + scoped delta read

// Mark a layout area (or widget) as a Scroll Target for this frame: it opts into mouse-wheel routing over its
// own rect. Call once per frame, inside the area's layout scope, for the id you want to read the wheel over.
// Routing is consume-takes-all: each frame's wheel goes to the single topmost
// Scroll Target under the cursor (the hot element) and to no one else - an inner target shadows its ancestors.
void ui_scroll_target(FlLayoutId layout_id);

// Read this frame's wheel delta scoped to a Scroll Target - the mouse_wheel_delta(id) query.
// Returns the per-frame delta only when layout_id is the hot Scroll Target (so two stacked targets never both
// see the same tick), and {0, 0} otherwise. There is no absolute wheel value: the hardware emits deltas and any
// cumulative offset (a timeline's scroll position, a list's scroll-y) is the caller's to accumulate. Symmetric
// with ui_mouse_position; coexists with the unscoped global ui_get_mouse_wheel().
FlVec2 ui_mouse_wheel_delta(FlLayoutId layout_id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Scaling API

// Set the absolute UI scale: authored logical pixels are multiplied by ui_scale (chrome) and text_scale
// (type). Callable any time (DPI/zoom/resolution change); recomposes on call. Default (1.0, 1.0) - logical ==
// physical. Composition (monitor DPI x user zoom, or a reference-fit policy via ui/reference_fit.h) is the
// app's job: it pushes the product here. A font handle names an authored size; text_scale is
// multiplied in per draw.
void ui_set_scale(float ui_scale, float text_scale);

// Set the display layout area in physical pixels. This is the rendering resolution only - it has no scale side
// effect (set scale independently via ui_set_scale). Call it when the window/display resolution changes.
void ui_set_display_resolution(int width, int height);

// Get the current absolute UI scale (1.0 = logical == physical).
float ui_get_scale(void);

// Get the current absolute text scale.
float ui_get_text_scale(void);

// Get the current context's Painter (custom-drawing API, ui/painter.h). Null before ui_create / outside an
// active context.
struct Painter;
struct Painter* ui_get_painter(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register the process-wide default filesystem mount that fl_font_load / fl_image_load resolve against. The
// runner calls this once after creating the mount; pass NULL to clear it.
void ui_set_asset_mount(FlVfsMount* mount);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background Image API
//
// Set a background image that fills the entire screen behind all UI elements.
// The background replaces the framebuffer clear.

/// Set background image from async loaded image handle
/// Pass IMAGE_HANDLE_INVALID to clear the background
void ui_background_set(ImageHandle handle);

/// Set background visibility/alpha (0=transparent, 255=opaque, default: 255)
void ui_background_set_alpha(u8 alpha);

/// Set clear color (used when no background image is set)
/// Should match theme surface.base color for visual consistency
void ui_background_set_clear_color(u8 r, u8 g, u8 b);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background Blend API
//
// Composite two background images with a SIMD-optimized lerp. Stateless: the caller passes both handles and
// the mix t every frame.

/// Blend two background images this frame: t mixes prev (0) toward current (255)
void ui_background_set_blend(ImageHandle prev, ImageHandle current, u8 t);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Modal Popup API
//
// Modal dialogs block input to widgets outside the modal scope.
//
// fl_modal_begin_frame / fl_modal_begin / fl_modal_end / fl_modal_close are public fl_* symbols
// defined in ui.c against the generated <flowi/ui/modal.h> prototypes.
// Usage (call begin_frame at frame start, begin/end around the body, close to dismiss):
//   if (dialog_is_visible) fl_modal_begin_frame(MY_DIALOG_SCOPE_ID);
//   fl_modal_begin(MY_DIALOG_SCOPE_ID); /* render modal widgets */ fl_modal_end();
//   fl_modal_close();

// Check if any modal is currently active (blocking input).
bool ui_modal_is_active(void);
