// fl_* public API forwarders - the thin hand-written bodies behind the api_gen direct API. Every
// fl_<domain>_* here forwards to the real ui_* / text_* / font_* / image_* free function that does the work;
// fl_format_frame_string is the one variadic exception (a forwarding function cannot pass varargs).

#include "ui.h"
#include <platform/input_types.h>
#include <core/sprintf.h>
#include "font_cache.h"
// Prototypes for the fl_<domain>_* bodies defined in this file.
#include <flowi/ui/background.h>
#include <flowi/ui/button.h>
#include <flowi/ui/checkbox.h>
#include <flowi/ui/chip.h>
#include <flowi/ui/cursor.h>
#include <flowi/ui/dropdown.h>
#include <flowi/ui/dynamic_image.h>
#include <flowi/ui/font.h>
#include <flowi/ui/format.h>
#include <flowi/ui/image.h>
#include <flowi/ui/item_behavior.h>
#include <flowi/ui/link.h>
#include <flowi/ui/painter.h>
#include <flowi/ui/pill.h>
#include <flowi/ui/progress_bar.h>
#include <flowi/ui/radio_group.h>
#include <flowi/ui/scrollbar.h>
#include <flowi/ui/slider.h>
#include <flowi/ui/spacer.h>
#include <flowi/ui/switch.h>
#include <flowi/ui/tab_bar.h>
#include <flowi/ui/theme.h>
#include <flowi/ui/text.h>
#include <flowi/ui/text_input.h>
#include <flowi/ui/ui_floating.h>
#include <flowi/ui/ui_input.h>
#include <flowi/ui/ui_layout.h>
#include <flowi/ui/ui_list.h>
#include <flowi/ui/ui_menu.h>
#include <flowi/ui/ui_scroll.h>
#include <flowi/ui/ui_selection.h>
#include "image.h"
#include "list_uniform.h"
#include "painter.h"
#include <ui/render_backend.h>
#include "ui_list.h"
#include "ui_menu.h"
#include "ui_message_box.h"
#include "ui_popup.h"
#include "ui_private.h"
#include "ui_scroll.h"
#include "ui_text_input.h"
#include "ui_tooltip.h"
#include <ui/ui_theme.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Button / Item behavior forwarders (button.def)

FlSignal fl_button_impl(FlString label, FlWidgetConfig config) {
    return ui_button_impl(label, config);
}

FlSignal fl_button_image_impl(FlString label, FlImageHandle image, FlWidgetConfig config) {
    return ui_button_with_image_impl(label, image, config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Item::behavior (item_behavior.def): translates the flag-only public surface into the internal config-taking
// call. FlItemFlags_auto_focus maps onto cfg.auto_focus (it takes effect only alongside
// FlItemFlags_focusable); cfg.focusable zeroes to FlFocusable_Default, so the flag alone decides.

FlSignal fl_item_behavior(FlLayoutId id, FlItemFlags flags) {
    FlWidgetConfig cfg = { .auto_focus = (flags & FlItemFlags_auto_focus) != 0 };
    return ui_item_behavior(id, flags, cfg);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TextInput (text_input.def).

FlTextInputState* fl_text_input_create(uint32_t capacity) {
    return ui_text_input_create(capacity);
}

void fl_text_input_destroy(FlTextInputState* state) {
    ui_text_input_destroy(state);
}

void fl_text_input_set_text(FlTextInputState* state, FlString text) {
    ui_text_input_set_text(state, text);
}

FlString fl_text_input_get_text(FlTextInputState* state) {
    return ui_text_input_get_text(state);
}

void fl_text_input_clear(FlTextInputState* state) {
    ui_text_input_clear(state);
}

// The parenthesised name suppresses the function-like macro fl_text_input_show in text_input.h.
FlSignal(fl_text_input_show)(FlString label, FlTextInputState* state) {
    return ui_text_input(label, state, (FlTextInputConfig) { 0 });
}

FlSignal fl_text_input_show_impl(FlString label, FlTextInputState* state, FlTextInputConfig config) {
    return ui_text_input(label, state, config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slider::float / Slider::int (slider.def). format is a NUL-terminated C string, so there is no
// FlString-to-cstr bridging here.

FlSignal fl_slider_float_impl(FlString label, float* value, float min, float max, FlSliderConfig config) {
    return ui_slider_float_impl(label, value, min, max, config);
}

FlSignal fl_slider_int_impl(FlString label, int* value, int min, int max, FlSliderConfig config) {
    return ui_slider_int_impl(label, value, min, max, config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hand-written bodies for the generated Cursor api.

void fl_cursor_set(FlCursor cursor) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    self->requested_cursor = cursor;
}

FlCursor fl_cursor_get_requested(void) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return (FlCursor) { 0 };
    }
    return self->requested_cursor;
}

void fl_cursor_set_visible(bool visible) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    self->cursor_visible = visible;
}

bool fl_cursor_get_visible(void) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return true; // the context default (ui_create), not the zero value - nothing has hidden the cursor
    }
    return self->cursor_visible;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hand-written bodies for the generated Painter api. ui_get_painter() returns null outside an open layout
// element and every primitive no-ops (or returns false) on a null painter, so the off-context case needs no
// check here.

// The one home of the font-id zero semantic (0 / FontId::INVALID = active font), shared by text draw, styled
// draw, measurement, and font metrics.
static FlFontId resolve_font_id(const FlUiCtx* self, FlFontId font_id) {
    return font_id != 0 ? font_id : self->active_font;
}

void fl_painter_set_effect(FlEffect effect) {
    painter_set_effect(ui_get_painter(), effect);
}

void fl_painter_rect(float x, float y, float w, float h, FlColor color) {
    painter_rect(ui_get_painter(), x, y, w, h, color);
}

void fl_painter_rect_rounded(float x, float y, float w, float h, float radius, FlColor color) {
    painter_rect_rounded(ui_get_painter(), x, y, w, h, radius, color);
}

void fl_painter_gradient_rect(float x, float y, float w, float h, FlColor top_left, FlColor top_right,
                              FlColor bottom_left, FlColor bottom_right) {
    painter_gradient_rect(ui_get_painter(), x, y, w, h, top_left, top_right, bottom_left, bottom_right);
}

void fl_painter_rect_styled(float x, float y, float w, float h, FlRectConfig config) {
    painter_rect_styled(ui_get_painter(), x, y, w, h, config);
}

void fl_painter_hline(float x, float y, float length, FlColor color) {
    painter_hline(ui_get_painter(), x, y, length, color);
}

void fl_painter_vline(float x, float y, float length, FlColor color) {
    painter_vline(ui_get_painter(), x, y, length, color);
}

// The two text draws are the exception: resolving the font-id zero semantic reads the context itself, so they
// carry the file's standard null-context guard on top of the painter's own no-op.

void fl_painter_text(float x, float y, FlString text, uint32_t font, FlColor color) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    painter_text(ui_get_painter(), x, y, text, resolve_font_id(self, font), color);
}

void fl_painter_text_styled(float x, float y, FlString text, FlTextStyle style, FlColor color) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    style.font_id = resolve_font_id(self, style.font_id);
    painter_text_styled(ui_get_painter(), x, y, text, style, color);
}

bool fl_painter_image(float x, float y, float w, float h, uint32_t image, FlColor color) {
    return painter_image(ui_get_painter(), x, y, w, h, image, color);
}

bool fl_painter_image_sub(float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                          FlImageHandle image, FlColor color) {
    return painter_image_sub(ui_get_painter(), dx, dy, dw, dh, sx, sy, sw, sh, image, color);
}

void fl_painter_circle(float cx, float cy, float radius, FlColor color, FlColor border_color, float border_width) {
    painter_circle(ui_get_painter(), cx, cy, radius, color, border_color, border_width);
}

void fl_painter_circle_stroked(float cx, float cy, float radius, FlColor color, FlColor border_color,
                               float border_width, FlStrokeStyle stroke) {
    painter_circle_stroked(ui_get_painter(), cx, cy, radius, color, border_color, border_width, stroke);
}

void fl_painter_diamond(float cx, float cy, float rx, float ry, FlColor color, FlColor border_color,
                        float border_width) {
    painter_diamond(ui_get_painter(), cx, cy, rx, ry, color, border_color, border_width);
}

void fl_painter_arc(float cx, float cy, float radius, float thickness, float start_angle, float sweep, FlColor color,
                    uint32_t flags) {
    painter_arc(ui_get_painter(), cx, cy, radius, thickness, start_angle, sweep, color, flags);
}

void fl_painter_polyline(const FlPolyPoint* points, uint32_t points_size, float width, FlColor color, uint32_t flags,
                         FlColor fill_color, float baseline_y) {
    painter_polyline(ui_get_painter(), points, points_size, width, color, flags, fill_color, baseline_y);
}

void fl_painter_bars(float baseline_y, const FlBar* bars, uint32_t bars_size, FlColor color) {
    painter_bars(ui_get_painter(), baseline_y, bars, bars_size, color);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hand-written bodies for the generated Text api. emit is a plain layout element in the theme primary
// color; styled consumes the authored FlTextConfig directly.

void fl_text_emit(FlString text) {
    ui_text(text);
}

void fl_text_styled(FlString text, FlTextConfig config) {
    ui_text_styled_impl(text, config);
}

FlTextMetrics fl_text_measure(FlString text, FlTextStyle style) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return (FlTextMetrics) { 0 };
    }
    FlFontId font_id = resolve_font_id(self, style.font_id);
    return font_cache_measure_text(self->shared->font_cache, text, font_id, style.font_size, style.letter_spacing);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The scroll (fl_scroll_area_*), floating (fl_floating_*) and layout (fl_layout_*) families are defined
// directly in ui/ui_scroll.c and ui/layout.c - no forwarder here.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UiTheme (theme.def). The palette crosses as a caller-owned pointer (flowi stores it, so it must outlive the
// frame); the override crosses by value.

void fl_theme_update(const FlThemeColors* colors) {
    ui_theme_update(colors);
}

void fl_theme_push(FlThemeOverride theme_override) {
    ui_theme_push(g_ui_ctx, theme_override);
}

void fl_theme_pop(void) {
    ui_theme_pop(g_ui_ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UiBackground (background.def). The ui_background_* callees own the g_ui_ctx no-op guard. Image handles cross
// as FlImageHandle and pass through numerically to the internal ImageHandle.

void fl_background_set(FlImageHandle handle) {
    ui_background_set(handle);
}

void fl_background_set_alpha(uint8_t alpha) {
    ui_background_set_alpha(alpha);
}

void fl_background_set_clear_color(uint8_t r, uint8_t g, uint8_t b) {
    ui_background_set_clear_color(r, g, b);
}

void fl_background_set_blend(FlImageHandle prev, FlImageHandle current, uint8_t t) {
    ui_background_set_blend(prev, current, t);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame API. The frame envelope owns UI opacity; background composition has its own controls.

void fl_frame_set_alpha(uint8_t alpha) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    self->global_alpha = alpha;
}

float fl_frame_delta_time(void) {
    return ui_get_delta_time();
}

FlVec2 fl_frame_screen_size(void) {
    return ui_get_screen_size();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UiInput (ui_input.def). These act on the global UI context, so there is no receiver to thread.

void fl_input_scroll_target(FlLayoutId id) {
    ui_scroll_target(id);
}

FlVec2 fl_input_mouse_wheel_delta(FlLayoutId id) {
    return ui_mouse_wheel_delta(id);
}

FlVec2 fl_input_mouse_wheel(void) {
    return ui_get_mouse_wheel();
}

FlKeyModifier fl_input_keyboard_modifiers(void) {
    return (FlKeyModifier)ui_keyboard_modifiers();
}

FlVec2 fl_input_mouse_position(FlLayoutId id) {
    return ui_mouse_position(id);
}

bool fl_input_mouse_pressed(FlMouseButton button) {
    return ui_mouse_pressed(button);
}

bool fl_input_mouse_down(FlMouseButton button) {
    return ui_mouse_down(button);
}

bool fl_input_mouse_released(FlMouseButton button) {
    return ui_mouse_released(button);
}

bool fl_input_key_pressed(FlKey key) {
    return ui_key_pressed(key);
}

// The one slot without a ui_* callee: nav is public by name, so fl_nav_down_pressed in ui.c IS the
// implementation this forwards to.
bool fl_input_nav_down_pressed(void) {
    return fl_nav_down_pressed();
}

uint32_t fl_input_dropped_file_count(void) {
    return ui_dropped_file_count();
}

FlString fl_input_dropped_file(uint32_t index) {
    return ui_dropped_file(index);
}

FlVec2 fl_input_dropped_file_position(uint32_t index) {
    return ui_dropped_file_position(index);
}

bool fl_input_key_down(FlKey key) {
    return ui_key_down(key);
}

bool fl_input_key_released(FlKey key) {
    return ui_key_released(key);
}

bool fl_input_text_input_active(void) {
    return ui_text_input_active();
}

FlElementState fl_input_get_element_state(FlLayoutId id) {
    return ui_get_element_state(id);
}

FlElementState fl_input_get_element_state_by_id(FlElementId id) {
    return ui_get_element_state_by_id(id);
}

float fl_input_get_scale(void) {
    return ui_get_scale();
}

FlElementId fl_input_hot_id(void) {
    return ui_hot_id();
}

FlElementId fl_input_hashed_id(FlLayoutId id) {
    return ui_layout_id_to_clay_id(id);
}

void fl_input_mouse_hidden_drag_begin(float scale) {
    ui_mouse_hidden_drag_begin(scale);
}

void fl_input_mouse_hidden_drag_end(void) {
    ui_mouse_hidden_drag_end();
}

FlVec2 fl_input_mouse_drag_delta(void) {
    return ui_mouse_drag_delta();
}

FlVec2 fl_input_mouse_delta(void) {
    return ui_mouse_delta();
}

bool fl_input_gamepad_connected(uint32_t slot) {
    return ui_gamepad_connected(slot);
}

bool fl_input_gamepad_button_down(uint32_t slot, FlGamepadButton button) {
    return ui_gamepad_button_down(slot, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_input_gamepad_button_pressed(u32 slot, FlGamepadButton button) {
    return ui_gamepad_button_pressed(slot, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_input_gamepad_button_released(u32 slot, FlGamepadButton button) {
    return ui_gamepad_button_released(slot, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 fl_input_gamepad_button_hold_duration(u32 slot, FlGamepadButton button) {
    return ui_gamepad_button_hold_duration(slot, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 fl_input_gamepad_axis(u32 slot, FlGamepadAxis axis) {
    return ui_gamepad_axis(slot, axis);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The input push surface, aimed at the current context; events pushed with no context are dropped. The
// multi-window form is fl_platform_input_push_* (flowi/platform/input.h).

void fl_input_push_key_event(FlKeyEvent event) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    fl_platform_input_push_key_event(self, event);
}

void fl_input_push_text_input_event(FlTextInputEvent event) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    fl_platform_input_push_text_input_event(self, event);
}

void fl_input_push_mouse_event(FlMouseEvent event) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    fl_platform_input_push_mouse_event(self, event);
}

void fl_input_push_gamepad_event(FlGamepadEvent event) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    fl_platform_input_push_gamepad_event(self, event);
}

void fl_input_push_window_event(FlWindowEvent event) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    fl_platform_input_push_window_event(self, event);
}

// Declarative List one-call face - the list slot on the UiInput domain (ui_input.def).
FlListResult fl_input_list(FlLayoutId id, const FlListItem* items, uint32_t count, FlSelectionSet* set,
                           FlListOpts opts) {
    return ui_list(id, items, count, set, opts);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UiList (ui_list.def).

FlListRange fl_list_begin(FlLayoutId id, const uint64_t* ordered_ids, uint32_t count, FlSelectionSet* set,
                          FlListConfig config) {
    return ui_list_begin(id, ordered_ids, count, set, config);
}

FlSignal fl_list_item_begin(uint64_t stable_id) {
    return ui_list_item_begin(stable_id);
}

void fl_list_item_end(void) {
    ui_list_item_end();
}

FlListResult fl_list_end(void) {
    return ui_list_end();
}

void fl_list_request_focus(FlLayoutId id, uint64_t cursor_id) {
    ui_list_request_focus(id, cursor_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ListUniform (list_uniform.def). The uniform list keeps its state on the UI context, so these thread
// g_ui_ctx's list_uniform context into the real list_uniform_* free functions - the caller never sees the
// receiver. The per-frame begin_update / end_update hooks stay internal: ui.c drives them around the frame.

FlListUniformRange fl_list_uniform_begin(FlString id, uint32_t total_count, FlListUniformOpts opts) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return (FlListUniformRange) { 0 };
    }
    return list_uniform_begin(self->list_uniform_ctx, id, total_count, opts);
}

void fl_list_uniform_item_begin(uint32_t index) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    list_uniform_item_begin(self->list_uniform_ctx, index);
}

void fl_list_uniform_item_end(void) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    list_uniform_item_end(self->list_uniform_ctx);
}

void fl_list_uniform_end(void) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    list_uniform_end(self->list_uniform_ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SelectionSet (ui_selection.def).

FlSelectionSet* fl_selection_set_create(uint32_t initial_capacity) {
    return selection_set_create(initial_capacity);
}

void fl_selection_set_destroy(FlSelectionSet* set) {
    selection_set_destroy(set);
}

bool fl_selection_set_contains(FlSelectionSet* set, uint64_t id) {
    return selection_set_contains(set, id);
}

void fl_selection_set_set(FlSelectionSet* set, uint64_t id, bool selected) {
    selection_set_set(set, id, selected);
}

void fl_selection_set_clear_all(FlSelectionSet* set) {
    selection_set_clear_all(set);
}

void fl_selection_set_set_all(FlSelectionSet* set, const uint64_t* ids, uint64_t count) {
    selection_set_set_all(set, ids, count);
}

void fl_selection_set_apply(FlSelectionSet* set, const FlSelectionRequest* reqs, uint64_t req_count,
                            const uint64_t* ordered_ids, uint64_t id_count) {
    selection_set_apply(set, reqs, req_count, ordered_ids, id_count);
}

uint64_t fl_selection_set_len(FlSelectionSet* set) {
    return selection_set_len(set);
}

uint64_t fl_selection_set_copy_ids(FlSelectionSet* set, uint64_t* out, uint64_t cap) {
    return selection_set_copy_ids(set, out, cap);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Font / Image (font.def / image.def). The font id / image handle round-trips as a u32 (FlFontId / ImageHandle
// are u32-wide).

static FlVfsMount* s_asset_mount = nullptr;

void ui_set_asset_mount(FlVfsMount* mount) {
    s_asset_mount = mount;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t fl_font_load(FlString path, uint16_t default_size) {
    if (s_asset_mount == nullptr) {
        return FONT_ID_INVALID;
    }
    return ui_font_load(s_asset_mount, path, default_size);
}

void fl_font_set(uint32_t font) {
    ui_font_set(font);
}

bool fl_font_is_loaded(uint32_t font) {
    return ui_font_is_loaded(font);
}

bool fl_font_ensure_loaded(void) {
    return ui_font_ensure_loaded();
}

FlFontMetrics fl_font_metrics(FlFontId font, float size) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return (FlFontMetrics) { 0 };
    }
    return font_cache_get_font_metrics(self->shared->font_cache, resolve_font_id(self, font), size);
}

uint32_t fl_image_load(FlString path) {
    if (s_asset_mount == nullptr) {
        return IMAGE_HANDLE_INVALID;
    }
    return image_load_async_with_options(s_asset_mount, path, (FlImageLoadOptions) { 0 });
}

uint32_t fl_image_load_mount(FlVfsMount* mount, FlString relative_path, FlImageLoadOptions options) {
    if (mount == nullptr) {
        return 0;
    }
    return image_load_async_with_options(mount, relative_path, options);
}

FlImageStatus fl_image_status(uint32_t handle) {
    return image_public_status(handle);
}

void fl_image_set_load_priority(uint32_t handle, FlImageLoadPriority priority) {
    image_set_load_priority(handle, (ImageLoadPriority)priority);
}

bool fl_image_is_ready(uint32_t handle) {
    return image_public_status(handle) == FlImageStatus_Ready;
}

FlImageHandle fl_image_create_from_pixels(const void* pixels, int32_t width, int32_t height, FlImageFormat format) {
    return image_create_from_pixels(pixels, width, height, format);
}

FlImageHandle fl_image_create_dedicated(const void* pixels, int32_t width, int32_t height, FlImageFormat format) {
    return image_create_dedicated(pixels, width, height, format);
}

bool fl_image_update_pixels(FlImageHandle handle, const void* pixels) {
    return image_update_pixels(handle, pixels);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generated widget API (checkbox / switch / radio_group / pill / chip / progress_bar / scrollbar / dropdown /
// tab_bar / link / image / dynamic_image / spacer defs). The array widgets take a `[*const String]` slice, so
// the generated prototype carries an options_size/tabs_size length companion that lowers to the internal
// int count (the slices are read-only, hence the const cast).

FlSignal fl_checkbox_impl(FlString label, bool* checked, FlCheckboxConfig config) {
    return ui_checkbox_impl(label, checked, config);
}

FlSignal fl_switch_impl(FlString label, bool* value, FlSwitchConfig config) {
    return ui_switch_impl(label, value, config);
}

FlSignal fl_radio_group_impl(const FlString* options, uint32_t options_size, int32_t* selected_index,
                             FlRadioConfig config) {
    return ui_radio_group_impl((FlString*)options, (int)options_size, selected_index, config);
}

FlSignal fl_pill_impl(FlString label, bool* selected, FlWidgetConfig config) {
    return ui_pill_impl(label, selected, config);
}

FlSignal fl_chip_impl(FlString label, bool* selected, FlWidgetConfig config) {
    return ui_chip_impl(label, selected, config);
}

void fl_progress_bar_impl(float fraction, FlProgressConfig config) {
    ui_progress_bar_impl(fraction, config);
}

FlSignal fl_scrollbar_impl(float* value, FlScrollbarConfig config) {
    return ui_scrollbar_impl(value, config);
}

void fl_dropdown_impl(FlString label, bool* is_open, const FlString* options, uint32_t options_size,
                      int32_t* selected_index, FlDropdownConfig config) {
    ui_dropdown_impl(label, is_open, (FlString*)options, (int)options_size, selected_index, config);
}

bool fl_tab_bar_impl(const FlString* tabs, uint32_t tabs_size, int32_t* selected_index, FlTabBarConfig config) {
    return ui_tab_bar_impl((FlString*)tabs, (int)tabs_size, selected_index, config);
}

FlSignal fl_link_impl(FlString text, FlLinkConfig config) {
    return ui_link_impl(text, config);
}

void fl_image(FlImageHandle handle, FlImageOpts opts) {
    ui_image_impl(handle, opts);
}

void fl_dynamic_image(FlDynamicImage* image, FlImageOpts opts) {
    ui_dynamic_image_impl(image, opts);
}

void fl_spacer(float height) {
    ui_spacer(height);
}

void fl_spacer_h(float width) {
    ui_spacer_h(width);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UiMenu / UiSubmenu / UiMenubar / UiMenubarMenu (ui_menu.def, four api blocks). The stateful check/radio kinds
// carry a caller bool*/int* out-pointer modelled in the IDL.

bool fl_menu_begin_impl(FlLayoutId menu_id, FlMenuConfig config) {
    return ui_menu_begin_impl(menu_id, config);
}

void fl_menu_end(void) {
    ui_menu_end_impl();
}

FlSignal fl_menu_item_impl(FlString label, FlMenuItemConfig config) {
    return ui_menu_item_impl(label, config);
}

FlSignal fl_menu_check_impl(FlString label, bool* checked, FlMenuItemConfig config) {
    return ui_menu_check_impl(label, checked, config);
}

FlSignal fl_menu_radio_impl(FlString label, int* selected, int value, FlMenuItemConfig config) {
    return ui_menu_radio_impl(label, selected, value, config);
}

void fl_menu_separator(void) {
    ui_menu_separator_impl();
}

void fl_menu_header(FlString label) {
    ui_menu_header_impl(label);
}

FlSignal fl_menu_region_begin_impl(FlLayoutId region_id, FlLayoutId menu_id, FlMenuRegionConfig config) {
    return ui_menu_region_begin_impl(region_id, menu_id, config);
}

void fl_menu_region_end(void) {
    ui_menu_region_end_impl();
}

void fl_menu_open(FlLayoutId menu_id, FlVec2 point) {
    ui_menu_open_impl(menu_id, point);
}

void fl_menu_close(FlLayoutId menu_id) {
    ui_menu_close_impl(menu_id);
}

bool fl_menu_is_open(FlLayoutId menu_id) {
    return ui_menu_is_open_impl(menu_id);
}

bool fl_submenu_begin_impl(FlString label, FlSubmenuConfig config) {
    return ui_submenu_begin_impl(label, config);
}

void fl_submenu_end(void) {
    ui_submenu_end_impl();
}

void fl_menubar_begin_impl(FlLayoutId bar_id, FlMenuBarConfig config) {
    ui_menubar_begin_impl(bar_id, config);
}

void fl_menubar_end(void) {
    ui_menubar_end_impl();
}

bool fl_menubar_is_open(FlLayoutId bar_id) {
    return ui_menubar_is_open_impl(bar_id);
}

bool fl_menubar_menu_begin_impl(FlString label, FlMenuBarMenuConfig config) {
    return ui_menubar_menu_begin_impl(label, config);
}

void fl_menubar_menu_end(void) {
    ui_menubar_menu_end_impl();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Popup API (popup.def). Forwards to ui_popup_begin / ui_popup_end, ui_tooltip_impl and ui_message_box_impl,
// which act on the global UI context - there is no receiver to thread.

void fl_popup_begin(FlLayoutId anchor_id, FlPopupConfig config) {
    ui_popup_begin(anchor_id, config);
}

void fl_popup_end(void) {
    ui_popup_end();
}

void fl_popup_tooltip(FlLayoutId anchor_id, FlString text, FlTooltipConfig config) {
    ui_tooltip_impl(anchor_id, text, config);
}

FlDialogResult fl_popup_message_box(FlString title, FlString message, bool* open, FlMessageBoxConfig config) {
    return ui_message_box_impl(title, message, open, config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The one variadic body in this file: formats into the frame arena.

FlString fl_format_frame_string(const char* format, ...) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return (FlString) { 0 };
    }

    va_list args;
    va_start(args, format);
    FlString message = vsprintf_arena(self->render_frame_allocator, format, args);
    va_end(args);

    return message;
}
