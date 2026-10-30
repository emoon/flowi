#include "ui.h"
#include "animation.h"
#include <core/arena.h>
#include <platform/input_types.h>
#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <core/perf_scope.h>
#include <core/sprintf.h>
#include <core/string.h>
#include "external/clay/clay.h"
#include "font_cache.h"
// api_gen direct API: prototypes for the fl_<domain>_* bodies defined in this file (a body whose
// signature drifts from its prototype fails to compile) and the lowercase formatting API.
#include <flowi/ui/background.h>
#include <flowi/ui/button.h>
#include <flowi/ui/cursor.h>
#include <flowi/ui/focused.h>
#include <flowi/ui/font.h>
#include <flowi/ui/format.h>
#include <flowi/ui/icon.h>
#include <flowi/ui/image.h>
#include <flowi/ui/item_behavior.h>
#include <flowi/ui/list_clipper.h>
#include <flowi/ui/modal.h>
#include <flowi/ui/painter.h>
#include <flowi/ui/slider.h>
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
#include <core/hashmap.h>
#include "image.h"
#include "image_registry.h"
#include "input_actions.h"
#include "layout.h"
#include "list_uniform.h"
#include "localization.h"
#include "pixel_snap.h"
#include "clay_to_render_ir.h"
#include "dock/dock_shared.h"
#include "dock/dock_workspace.h"
#include "painter.h"
#include <ui/render_backend.h>
#include "render_ir.h"
#include "ui_hit_test.h"
#include "ui_list.h"
#include "ui_menu.h"
#include "ui_navigation.h"
#include "ui_popup.h"
#include "ui_private.h"
#include "ui_scroll.h"
#include "ui_text_input.h"
#include "ui_tokens.h"
#include "ui_variant_helpers.h"
#include "ui_widget_internal.h"
#include "ui_virtual_keyboard.h"
#include <vfs/vfs.h>
#include <ui/ui_theme.h>
#include <stdio.h>
#include <string.h>

// The process-global UI context: non-null between ui_create and ui_destroy. Public entry points check it once
// with FL_VALIDATE(g_ui_ctx != nullptr); the background family no-ops on null instead.
FlUiCtx* g_ui_ctx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_ascii_quick(const char* data, i32 len) {
    for (i32 i = 0; i < len; i++) {
        if ((u8)data[i] >= 0x80) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Clay's measure callback. The width includes the run's glyph gaps (n-1) plus one trailing letterSpacing: Clay
// subtracts one letterSpacing from the unwrapped width of every text element, so a single-word run's element
// comes out exactly as wide as the glyphs are painted. Wrapped multi-word text keeps Clay's own word-gap rule
// (each word and each space is measured with that trailing gap), so a tracked wrapped run can lay out wider
// than it paints.
static Clay_Dimensions measure_text_function(Clay_StringSlice text, Clay_TextElementConfig* config, void* user_data) {
    FlUiCtx* ctx = (FlUiCtx*)user_data;

    if (!ctx || !ctx->shared->font_cache) {
        return (Clay_Dimensions) { 0, 0 };
    }

    // Clay_StringSlice carries no is_ascii flag, so scan for it here.
    FlString our_string = {
        .data = text.chars,
        .length = text.length,
        .is_ascii = is_ascii_quick(text.chars, text.length),
    };

    FlFontId font_id = (FlFontId)config->fontId;
    const float letter_spacing = (float)config->letterSpacing;

    FlTextMetrics m = font_cache_measure_text(ctx->shared->font_cache, our_string, font_id,
                                              (float)font_cache_quantize_size((u16)config->fontSize), letter_spacing);

    if (m.height == 0.0f) {
        return (Clay_Dimensions) { 0, 0 };
    }

    return (Clay_Dimensions) { m.width + letter_spacing, m.height };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Clay returns a safe default after invoking this, so a release build can carry the frame to completion.
static void handle_clay_errors(Clay_ErrorData errorData) {
    // Clay resets its own once-per-error latch in Clay_BeginLayout, so a persistent error reports every frame.
    static bool logged = false;
    if (!logged) {
        logged = true;
        log_error("Clay layout error (type %d): %.*s", (int)errorData.errorType, (int)errorData.errorText.length,
                  errorData.errorText.chars);
    }
    FL_ASSERT(false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Bring up the app-level shared resource tier: one per app, handed to each window's ui_create. The
// host-constructed backend's vtable is copied in.
UiShared* ui_shared_create(FlArena* arena, FlRenderBackend* backend) {
    FL_VALIDATE_RET(backend != nullptr, nullptr);
    UiShared* shared = arena_alloc_zero(arena, UiShared);
    shared->renderer = *backend;
    // The backend-agnostic readiness registry: the single source of image readiness/dimensions, fed by each
    // backend's realization ack. Holds no pixels - the SW realized-pixel pool belongs to the SW backend.
    shared->image_registry = image_registry_new(arena);
    // Base theme is injected by the Frontend after loading (ui_set_theme); null falls through to the default.
    shared->theme_colors = nullptr;

    // Persistent id-keyed widget-state store: keyed by the location-independent clay hashed id, allocated
    // from the shared arena so it outlives any single window and survives a Detachable Panel promote/demote.
    hashmap_new(&shared->item_state, arena, 256);
    hashmap_new(&shared->scroll_states, arena, 64);
    hashmap_new(&shared->widget_scroll_map, arena, 128);

    // App-global docking state: the workspace registry (entry 0 is the default/main workspace),
    // the injected host seam, and the deferred promote queue. Owns the workspaces' arenas.
    shared->dock = dock_shared_create();

#ifndef NDEBUG
    // The duplicate-id tracker is app-global: one id space across every window, cleared each frame
    // and populated as each window's subtree is laid out.
    hashmap_new(&shared->debug_id_tracker, arena, 256);
#endif
    return shared;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_shared_destroy(UiShared* shared) {
    if (!shared) {
        return;
    }

    if (shared->image_registry) {
        image_registry_destroy(shared->image_registry);
        shared->image_registry = nullptr;
    }

    // Tears down every registry workspace's tree and arenas.
    if (shared->dock) {
        dock_shared_destroy(shared->dock);
        shared->dock = nullptr;
    }

    localization_destroy(shared->localization);
    shared->localization = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlUiCtx* ui_create(FlArena* arena, UiShared* shared) {
    FL_VALIDATE_RET(shared != nullptr, nullptr);
    FlUiCtx* ctx = arena_alloc_zero(arena, FlUiCtx);
    ctx->persistant = arena;
    ctx->render_frame_allocator = arena_new();
    ctx->render_frame_allocator_state = arena_new();
#ifdef FLOWI_UI_INSPECTOR
    ctx->render_command_arena = arena_new();
#endif
    ctx->shared = shared;
    // Bind this window to the default dock workspace (registry entry 0); a promoted window is rebound to its own
    // workspace at materialization.
    ctx->dock_workspace = dock_shared_default(shared->dock);
    ctx->painter = painter_create(arena);

    ctx->scroll_area_stack = arena_alloc_array(arena, ScrollAreaStackEntry, SCROLL_AREA_STACK_MAX_DEPTH);
    ctx->scroll_area_stack_depth = 0;

    ui_actions_init(ctx);

    ctx->physical_keyboard_available = true;
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;
    ctx->vk_arena = arena_new();

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;

    ctx->global_alpha = 255;

    background_controller_init(&ctx->background);

    // Identity scale until the app pushes an absolute pair via ui_set_scale.
    ctx->ui_scale = 1.0f;
    ctx->text_scale = 1.0f;

    // Cursor starts visible (zero-init leaves requested_cursor at the arrow, the per-frame default).
    ctx->cursor_visible = true;

    // OS-focused until a focus-lost event says otherwise.
    ctx->input_state.window_focused = true;

    // Each window owns its own Clay context; ui_make_current re-selects it (with g_ui_ctx) before each window
    // emits its frame.
    u32 clay_mem_size = Clay_MinMemorySize();
    Clay_Arena clay_arena
        = Clay_CreateArenaWithCapacityAndMemory(clay_mem_size, arena_alloc_array(arena, u8, clay_mem_size));

    ctx->clay_ctx = Clay_Initialize(clay_arena, (Clay_Dimensions) { ctx->layout_width, ctx->layout_height },
                                    (Clay_ErrorHandler) { handle_clay_errors, nullptr });

    Clay_SetMeasureTextFunction(measure_text_function, ctx);

    // g_ui_ctx and Clay's current context always name the same window; ui_make_current / ui_destroy swap and
    // clear them as a pair.
    g_ui_ctx = ctx;

    ctx->list_uniform_ctx = list_uniform_create(arena);

    ctx->ui_list_ctx = ui_list_create(arena);

    return ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_destroy(FlUiCtx* ctx) {
    FL_VALIDATE(ctx != nullptr);

    if (ctx->list_uniform_ctx) {
        list_uniform_destroy(ctx->list_uniform_ctx);
        ctx->list_uniform_ctx = nullptr;
    }

    viewport3d_release_owner(ctx);

    if (ctx->painter) {
        painter_destroy(ctx->painter);
        ctx->painter = nullptr;
    }

    if (ctx->render_frame_allocator) {
        fl_arena_destroy(ctx->render_frame_allocator);
        ctx->render_frame_allocator = nullptr;
    }

    if (ctx->render_frame_allocator_state) {
        fl_arena_destroy(ctx->render_frame_allocator_state);
        ctx->render_frame_allocator_state = nullptr;
    }

    if (ctx->vk_arena) {
        fl_arena_destroy(ctx->vk_arena);
        ctx->vk_arena = nullptr;
    }

#ifdef FLOWI_UI_INSPECTOR
    if (ctx->render_command_arena) {
        fl_arena_destroy(ctx->render_command_arena);
        ctx->render_command_arena = nullptr;
        ctx->last_render_commands = nullptr;
        ctx->last_render_command_count = 0;
    }
#endif

    // Cleared as a pair, matching ui_create and ui_make_current. Clay_MinMemorySize and Clay_Initialize both read
    // the current context, so leaving it dangling at this context's freed arena would fault the next ui_create.
    if (Clay_GetCurrentContext() == ctx->clay_ctx) {
        Clay_SetCurrentContext(nullptr);
    }
    if (g_ui_ctx == ctx) {
        g_ui_ctx = nullptr;
    }

    // ctx itself lives in the caller's persistent arena; font_cache is owned by the application.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Select ctx as the active window context: swap both the core singleton and Clay's current layout
// context so layout/input/render-command emission all target this window's tree. Called before a window emits.
void ui_make_current(FlUiCtx* ctx) {
    FL_VALIDATE(ctx != nullptr);
    g_ui_ctx = ctx;
    Clay_SetCurrentContext(ctx->clay_ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_set_surface(FlUiCtx* ctx, FlSurface* surface) {
    FL_VALIDATE(ctx != nullptr);
    ctx->surface = surface;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_set_font_cache(FlUiCtx* ctx, struct FlFontCache* font_cache) {
    ctx->shared->font_cache = font_cache;

    // The backend rebinds and drops any realization tied to the previous cache.
    if (ctx->shared->renderer.set_font_cache) {
        ctx->shared->renderer.set_font_cache(ctx->shared->renderer.user_data, font_cache);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_set_theme(FlUiCtx* ctx, const struct FlThemeColors* colors) {
    FL_VALIDATE(ctx != nullptr);

    ctx->shared->theme_colors = colors;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Standard toolkit-chrome icons, indexed by FlIcon: VFS path + square render size in pixels.
static const struct {
    FlString path;
    u16 size;
} ICON_TABLE[FlIcon_Count] = {
    [FlIcon_Checkmark] = { FL_S_("images/icons/checkmark.svg"), 18 },      // fits checkbox inner size
    [FlIcon_ChevronDown] = { FL_S_("images/icons/chevron-down.svg"), 16 }, // for dropdowns
    [FlIcon_Warning] = { FL_S_("images/icons/warning.svg"), 18 },          // error states
};

void fl_icon_load(FlVfsMount* mount) {
    if (!g_ui_ctx || !mount) {
        return;
    }

    for_count(i, FlIcon_Count) {
        g_ui_ctx->shared->icons[i] = image_load_async(mount, ICON_TABLE[i].path, .target_width = ICON_TABLE[i].size,
                                                      .target_height = ICON_TABLE[i].size);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlImageHandle fl_icon_get(FlIcon icon) {
    if (!g_ui_ctx || (unsigned)icon >= FlIcon_Count) {
        return FL_IMAGE_HANDLE_INVALID;
    }
    return (FlImageHandle)g_ui_ctx->shared->icons[icon];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_theme_update(const struct FlThemeColors* colors) {
    ui_set_theme(g_ui_ctx, colors);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const FlThemeColors* fl_theme_colors(void) {
    return ui_get_theme_colors(g_ui_ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t fl_focused_id(void) {
    return g_ui_ctx ? g_ui_ctx->focused_id : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Install (or clear) the app's UI feedback hook. One hook serves every window. No-op before any
// ui_create (no shared tier to hold it).
void fl_ui_set_feedback_hook(FlUiFeedbackCallback cb, void* user_data) {
    if (g_ui_ctx == nullptr) {
        return;
    }
    g_ui_ctx->shared->feedback_hook = cb;
    g_ui_ctx->shared->feedback_user_data = user_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The bottom of the theme resolution stack: the host-injected colors, or the built-in default.
static const FlThemeColors* ui_theme_base_colors(const FlUiCtx* ctx) {
    if (!ctx || !ctx->shared->theme_colors) {
        static const FlThemeColors default_colors = {
            .interactive.base = FL_THEME_DEFAULT_INTERACTIVE_BASE,
            .interactive.hover = FL_THEME_DEFAULT_INTERACTIVE_HOVER,
            .interactive.press = FL_THEME_DEFAULT_INTERACTIVE_PRESS,
            .interactive.focus = FL_THEME_DEFAULT_INTERACTIVE_FOCUS,
            .interactive.disabled = FL_THEME_DEFAULT_INTERACTIVE_DISABLED,
            .surface.base = FL_THEME_DEFAULT_SURFACE_BASE,
            .surface.elevated = FL_THEME_DEFAULT_SURFACE_ELEVATED,
            .surface.overlay = FL_THEME_DEFAULT_SURFACE_OVERLAY,
            .surface.selected = FL_THEME_DEFAULT_SURFACE_SELECTED,
            .border.default_color = FL_THEME_DEFAULT_BORDER_DEFAULT,
            .border.focus = FL_THEME_DEFAULT_BORDER_FOCUS,
            .border.separator = FL_THEME_DEFAULT_BORDER_SEPARATOR,
            .text.primary = FL_THEME_DEFAULT_TEXT_PRIMARY,
            .text.secondary = FL_THEME_DEFAULT_TEXT_SECONDARY,
            .text.accent = FL_THEME_DEFAULT_TEXT_ACCENT,
            .feedback.success = FL_THEME_DEFAULT_FEEDBACK_SUCCESS,
            .feedback.warning = FL_THEME_DEFAULT_FEEDBACK_WARNING,
            .feedback.error = FL_THEME_DEFAULT_FEEDBACK_ERROR,
            .feedback.info = FL_THEME_DEFAULT_FEEDBACK_INFO,
        };
        return &default_colors;
    }

    return ctx->shared->theme_colors;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Theme-color resolution seam: the innermost scoped override (ui_theme_push) if one is active, otherwise the
// base theme.

const FlThemeColors* ui_get_theme_colors(FlUiCtx* ctx) {
    if (ctx && ctx->theme_override_depth > 0) {
        return ctx->theme_override_stack[ctx->theme_override_depth - 1];
    }
    return ui_theme_base_colors(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Push starts from the currently resolved theme so the override inherits every slot it does not touch - that
// is what makes nested overrides stack (an inner push layers on top of the outer's merged result).

void ui_theme_push(FlUiCtx* ctx, FlThemeOverride override) {
    FL_VALIDATE(ctx != nullptr);
    FL_VALIDATE(ctx->theme_override_depth < UI_THEME_OVERRIDE_MAX);

    const FlThemeColors merged = fl_theme_merge_override(ui_get_theme_colors(ctx), &override);

    const int depth = ctx->theme_override_depth;
    ctx->theme_override_storage[depth] = merged;
    ctx->theme_override_stack[depth] = &ctx->theme_override_storage[depth];
    ctx->theme_override_depth = depth + 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resolve the theme a widget renders against: the seam-resolved theme (including any active subtree overrides) with
// the widget's own sparse override merged on top. Never returns null. With no widget override it returns the seam
// theme directly (no copy); when an override applies, the merged result is written into caller-owned scratch.

const FlThemeColors* ui_resolve_widget_theme(FlUiCtx* self, const FlThemeOverride* override, FlThemeColors* scratch) {
    const FlThemeColors* theme = ui_get_theme_colors(self);
    if (override && override->slots) {
        *scratch = fl_theme_merge_override(theme, override);
        return scratch;
    }
    return theme;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_theme_pop(FlUiCtx* ctx) {
    FL_VALIDATE(ctx != nullptr);
    FL_VALIDATE(ctx->theme_override_depth > 0);
    ctx->theme_override_depth--;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns null in the pre-init window; see the g_ui_ctx declaration for the guarding rule callers must follow.
FlUiCtx* ui_get_ctx(void) {
    return g_ui_ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_ctx_set_dock_workspace(FlUiCtx* ctx, struct FlDockWorkspace* ws) {
    if (ctx != nullptr) {
        ctx->dock_workspace = ws; // non-owning: the shared registry owns the workspace
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The current context's Painter, so the shape primitives (painter_circle, ...) are reachable without threading
// one. Valid only inside an open painting scope (a layout element); returns null before ui_create, with no
// active context, or outside any scope - paint draws inside an element's immediate-mode scope, never on top.
struct Painter* ui_get_painter(void) {
    if (g_ui_ctx == nullptr || g_ui_ctx->painter == nullptr) {
        return nullptr;
    }
    if (painter_scope_depth(g_ui_ctx->painter) == 0) {
        return nullptr; // outside any open layout element - there is nowhere to attach paint
    }
    return g_ui_ctx->painter;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_update_begin(FlUiCtx* self) {
    perf_scope_function();

    // Must precede the auto-close check below.
    self->popup_closed_this_frame = false;

    // Reset the per-frame cursor request to the arrow. Widgets re-request a shape each frame they want one
    // (last-writer-wins); when nobody does, the app flushes the arrow. Visibility is sticky and not reset here.
    self->requested_cursor = (FlCursor) { .shape = FlCursorShape_Arrow };

    // Runs AFTER fl_platform_input_update() has captured mouse_pressed_this_frame.
    ui_popup_check_auto_close(self);

    Clay_Vector2 pointer_pos = { self->input_state.mouse.x, self->input_state.mouse.y };
    bool pointer_down = self->input_state.mouse.buttons[FlMouseButton_Left];
    Clay_SetPointerState(pointer_pos, pointer_down);

    // Scroll containers must be updated BEFORE BeginLayout (as per Clay docs), from the cached wheel values
    // (set in fl_platform_input_update before clearing).
    Clay_Vector2 scroll_delta = { -self->input_state.mouse.wheel_x_cached, -self->input_state.mouse.wheel_y_cached };

    // The previous frame's hot_id decides the wheel's arbiter.
    bool pointer_over_scroll_widget = false;
    if (self->hot_id != 0) {
        ItemState* hot_item = hashmap_get(&self->shared->item_state, self->hot_id);
        if (hot_item && (hot_item->flags & FlItemFlags_scroll_target)) {
            pointer_over_scroll_widget = true;
        }
    }

    // The wheel goes to exactly one arbiter: a hot Scroll Target widget owns it (Clay gets a zero delta and the
    // cache stays intact for the widget's read); otherwise Clay's scroll containers get it.
    if (!pointer_over_scroll_widget) {
        Clay_UpdateScrollContainers(true, scroll_delta, self->delta_time);
    } else {
        Clay_UpdateScrollContainers(true, (Clay_Vector2) { 0, 0 }, self->delta_time);
    }

    Clay_BeginLayout();

#ifndef NDEBUG
    // Clear the app-global duplicate-id tracker unless an FlApp owns the tick loop (ui_app_frame_begin clears it).
    if (!self->shared->debug_id_tracker_app_owned) {
        hashmap_clear(&self->shared->debug_id_tracker);
    }
#endif

    image_begin_frame();
    font_cache_update(self->shared->font_cache);
    fl_arena_rewind(self->render_frame_allocator);
    fl_arena_rewind(self->render_frame_allocator_state);

    // The frame arenas now hold this frame's storage, so fl_frame_str is legal from here until ui_update_end.
    self->frame_open = true;

    painter_begin_frame(self->painter, self->shared->font_cache, self->ui_scale, self->text_scale,
                        self->render_frame_allocator_state);

    list_uniform_begin_update(self->list_uniform_ctx);

    // The stack's memory went with the arena rewind above.
    self->popup_stack_count = 0;

    // Scoped theme overrides are frame-local; a caller that forgets to pop must not leak the override into the
    // next frame. Reset to the base theme at the top of every frame.
    self->theme_override_depth = 0;

    self->frame_counter++;

    // Worst case is every item in the hashmap being focusable, plus 4x headroom for items created during the
    // frame (sliders create multiple sub-items), with a 256 floor for page transitions that grow the item count.
    self->focus_order_count = 0;
    u32 current_items = hashmap_count(&self->shared->item_state);
    u32 min_capacity = 256;
    u32 max_focusable
        = current_items > 0 ? (current_items * 4 > min_capacity ? current_items * 4 : min_capacity) : min_capacity;
    if (max_focusable > 0) {
        self->focus_order = arena_alloc_array(self->render_frame_allocator, u32, max_focusable);
        self->focus_order_capacity = max_focusable;
    } else {
        self->focus_order = nullptr;
        self->focus_order_capacity = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_app_frame_begin(FlUiCtx* self) {
    // Finishes an in-flight string table and applies a pending language switch before any text resolves.
    localization_frame_begin(self->shared->localization);

    self->shared->item_epoch_app_owned = true;

#ifndef NDEBUG
    // The duplicate-id tracker is app-global: cleared once per app tick, it accumulates every window's ids.
    self->shared->debug_id_tracker_app_owned = true;
    hashmap_clear(&self->shared->debug_id_tracker);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 ui_get_mouse_wheel(void) {
    return (FlVec2) { g_ui_ctx->input_state.mouse.wheel_x_cached, g_ui_ctx->input_state.mouse.wheel_y_cached };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_nav_up_pressed(void) {
    return g_ui_ctx->nav_frame.nav_up;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_nav_down_pressed(void) {
    return g_ui_ctx->nav_frame.nav_down;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_consume_nav_up(void) {
    g_ui_ctx->nav_frame.nav_up = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_consume_nav_down(void) {
    g_ui_ctx->nav_frame.nav_down = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_consume_nav_left(void) {
    g_ui_ctx->nav_frame.nav_left = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_consume_nav_right(void) {
    g_ui_ctx->nav_frame.nav_right = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Input-state accessors: the range check precedes the array access. EDGE queries read the per-frame context
// snapshot; LEVEL queries read the raw array.

// Level read of a keyboard state-array member (keys/keys_pressed/keys_released).
#define key_state_get(member, key) \
    (((int)(key) < 0 || (int)(key) >= FlKey_Count) ? false : g_ui_ctx->input_state.keyboard.member[(key)])

// Edge read against a per-frame context snapshot (keys_*_this_frame).
#define key_edge_get(member, key) (((int)(key) < 0 || (int)(key) >= FlKey_Count) ? false : g_ui_ctx->member[(key)])

bool ui_key_pressed(FlKey key) {
    return key_edge_get(keys_pressed_this_frame, key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_key_down(FlKey key) {
    return key_state_get(keys, key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_key_released(FlKey key) {
    return key_edge_get(keys_released_this_frame, key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_text_input_active(void) {
    return g_ui_ctx->input_state.text_input_active;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 ui_keyboard_modifiers(void) {
    return g_ui_ctx->input_state.keyboard.modifiers;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 ui_dropped_file_count(void) {
    return g_ui_ctx->dropped_file_count_this_frame;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString ui_dropped_file(u32 index) {
    if (index >= g_ui_ctx->dropped_file_count_this_frame) {
        return (FlString) { .data = nullptr, .length = 0, .is_static = 0 };
    }
    return g_ui_ctx->dropped_files_this_frame[index];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 ui_dropped_file_position(u32 index) {
    if (index >= g_ui_ctx->dropped_file_count_this_frame) {
        return (FlVec2) { .x = 0.0f, .y = 0.0f };
    }
    return g_ui_ctx->dropped_file_positions_this_frame[index];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Read of a per-button bool array, under the same two rules as the keyboard accessors above. Callers pass the
// snapshot array (mouse_buttons_*_this_frame) for an edge and input_state.mouse.buttons[] for the held level.
#define mouse_button_state_get(array, button) \
    (((int)(button) < 0 || (int)(button) >= FlMouseButton_Count) ? false : (array)[(button)])

bool ui_mouse_pressed(FlMouseButton button) {
    return mouse_button_state_get(g_ui_ctx->mouse_buttons_pressed_this_frame, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_mouse_down(FlMouseButton button) {
    return mouse_button_state_get(g_ui_ctx->input_state.mouse.buttons, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_mouse_released(FlMouseButton button) {
    return mouse_button_state_get(g_ui_ctx->mouse_buttons_released_this_frame, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 ui_mouse_delta(void) {
    const MouseState* mouse = &g_ui_ctx->input_state.mouse;
    return (FlVec2) { mouse->delta_x, mouse->delta_y };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_gamepad_connected(u32 slot) {
    return slot < UI_INPUT_MAX_GAMEPADS && g_ui_ctx->input_state.gamepads[slot].connected;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An empty slot reads as "no buttons held".

bool ui_gamepad_button_down(u32 slot, FlGamepadButton button) {
    if (slot >= UI_INPUT_MAX_GAMEPADS || (u32)button >= FlGamepadButton_Count) {
        return false;
    }

    const GamepadState* pad = &g_ui_ctx->input_state.gamepads[slot];
    return pad->connected && pad->buttons[button];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The pad's edge, or false when the slot or button is out of range or the slot is empty. Callers pass the
// per-frame snapshot (gamepad_buttons_*_this_frame), not the raw arrays fl_platform_input_update has cleared.
static bool gamepad_edge_get(bool edges[UI_INPUT_MAX_GAMEPADS][FlGamepadButton_Count], u32 slot,
                             FlGamepadButton button) {
    return ui_gamepad_connected(slot) && (u32)button < FlGamepadButton_Count && edges[slot][button];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_gamepad_button_pressed(u32 slot, FlGamepadButton button) {
    return gamepad_edge_get(g_ui_ctx->gamepad_buttons_pressed_this_frame, slot, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_gamepad_button_released(u32 slot, FlGamepadButton button) {
    return gamepad_edge_get(g_ui_ctx->gamepad_buttons_released_this_frame, slot, button);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 ui_gamepad_button_hold_duration(u32 slot, FlGamepadButton button) {
    if (!ui_gamepad_connected(slot) || (u32)button >= FlGamepadButton_Count) {
        return 0.0f;
    }

    return g_ui_ctx->input_state.gamepads[slot].button_hold_duration[button];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 ui_gamepad_axis(u32 slot, FlGamepadAxis axis) {
    if (!ui_gamepad_connected(slot) || (u32)axis >= FlGamepadAxis_Count) {
        return 0.0f;
    }

    return g_ui_ctx->input_state.gamepads[slot].axes[axis];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_mouse_hidden_drag_begin(float scale) {
    g_ui_ctx->hidden_drag_active = true;
    g_ui_ctx->hidden_drag_scale = scale;
    // Grab the current absolute mouse position as the press point - the warp-back target on end.
    g_ui_ctx->hidden_drag_press_x = g_ui_ctx->input_state.mouse.x;
    g_ui_ctx->hidden_drag_press_y = g_ui_ctx->input_state.mouse.y;
    g_ui_ctx->relative_mouse_mode = true;
    // A fresh begin cancels any warp the previous end left pending but unflushed.
    g_ui_ctx->warp_pending = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_mouse_hidden_drag_end(void) {
    g_ui_ctx->hidden_drag_active = false;
    g_ui_ctx->relative_mouse_mode = false;
    // One-shot warp back to where the drag began; the app's flush consumes it via ui_take_pending_warp.
    g_ui_ctx->warp_pending = true;
    g_ui_ctx->warp_x = g_ui_ctx->hidden_drag_press_x;
    g_ui_ctx->warp_y = g_ui_ctx->hidden_drag_press_y;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 ui_mouse_drag_delta(void) {
    if (!g_ui_ctx->hidden_drag_active) {
        return (FlVec2) { 0.0f, 0.0f };
    }
    return (FlVec2) { g_ui_ctx->input_state.mouse.raw_delta_x_cached * g_ui_ctx->hidden_drag_scale,
                      g_ui_ctx->input_state.mouse.raw_delta_y_cached * g_ui_ctx->hidden_drag_scale };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_get_relative_mouse_mode(void) {
    return g_ui_ctx->relative_mouse_mode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_take_pending_warp(float* out_x, float* out_y) {
    if (!g_ui_ctx->warp_pending) {
        return false;
    }
    *out_x = g_ui_ctx->warp_x;
    *out_y = g_ui_ctx->warp_y;
    g_ui_ctx->warp_pending = false;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_action_pressed(FlAction action) {
    return ui_action_pressed(g_ui_ctx, action);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlNavFrame fl_get_nav_frame(void) {
    return g_ui_ctx->nav_frame;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ui_get_delta_time(void) {
    return g_ui_ctx ? g_ui_ctx->delta_time : 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Modal Popup API

void fl_modal_begin_frame(u32 scope_id) {
    if (scope_id == 0) {
        return;
    }

    // Marks the modal active, which also prevents popup cleanup in ui_update_end.
    g_ui_ctx->modal_scope_id = scope_id;

    if (ui_focus_scope_arm(g_ui_ctx, scope_id, false)) {
        g_ui_ctx->popup_needs_initial_focus = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_modal_begin(u32 scope_id) {
    if (scope_id == 0) {
        return;
    }

    fl_modal_begin_frame(scope_id);

    // Plus the current popup scope, so widgets rendered inside the modal are scoped to it.
    g_ui_ctx->current_popup_scope_id = scope_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_modal_end(void) {
    // The focus scope stays active until the modal closes.
    g_ui_ctx->current_popup_scope_id = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_modal_close(void) {
    u32 focused_id = g_ui_ctx->focused_id;
    bool had_saved_focus = g_ui_ctx->saved_focused_id != 0;
    ui_focus_scope_release(g_ui_ctx);
    if (!had_saved_focus) {
        g_ui_ctx->focused_id = focused_id;
    }
    g_ui_ctx->saved_focused_id = 0;
    g_ui_ctx->modal_scope_id = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_modal_is_active(void) {
    return g_ui_ctx->popup_focus_scope_id != 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_set_focus_id(FlElementId id) {
    g_ui_ctx->focused_id = id;
    g_ui_ctx->should_scroll_to_focused = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_set_focus(FlLayoutId layout_id) {
    fl_set_focus_id(ui_layout_id_to_clay_id(layout_id));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns FlElementState with found=0 if the widget has no state entry.

FlElementState ui_get_element_state_by_id(u32 element_id) {
    ItemState* item = hashmap_get(&g_ui_ctx->shared->item_state, element_id);
    return item != nullptr ? item->state : (FlElementState) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlElementState ui_get_element_state(FlLayoutId layout_id) {
    return ui_get_element_state_by_id(ui_layout_id_to_clay_id(layout_id));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The single topmost-hovered element id this frame - the core's already-arbitrated hot_id (0 = nothing hovered).

u32 ui_hot_id(void) {
    FlUiCtx* ctx = g_ui_ctx;
    return ctx ? ctx->hot_id : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pointer position in an element's local logical (authoring) space - (mouse - origin) / ui_scale. The
// element rect lands in physical pixels (Clay lays out at the actual resolution) and the mouse is fed in that
// same physical space, so dividing the local delta by ui_scale converts back to the logical authoring space the
// Painter draws in - letting a canvas hit-test the cursor against its painted geometry at any UI scale.

FlVec2 ui_mouse_position(FlLayoutId layout_id) {
    FlElementState state = ui_get_element_state(layout_id);
    if (!state.found) {
        return (FlVec2) { 0 };
    }

    const float ui_scale = g_ui_ctx->ui_scale;
    const float inv_scale = ui_scale != 0.0f ? 1.0f / ui_scale : 0.0f;
    return (FlVec2) {
        .x = (g_ui_ctx->input_state.mouse.x - state.pos.x) * inv_scale,
        .y = (g_ui_ctx->input_state.mouse.y - state.pos.y) * inv_scale,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scroll Target registration. A plain layout area never enters item_state on its own - only ui_item_behavior
// creates entries, and item_state_update skips ids not already present. This inserts the area into item_state
// with FlItemFlags_scroll_target, computing the clay id exactly as the layout open path does, so the existing
// next_hot_id/hot_id arbitration picks it up. Call it once per frame for the area you read the wheel over.

static ItemState* get_or_create_key(FlUiCtx* self, u32 id);

void ui_scroll_target(FlLayoutId layout_id) {
    FlUiCtx* self = g_ui_ctx;
    u32 clay_id = ui_layout_id_to_clay_id(layout_id);
    ItemState* item = get_or_create_key(self, clay_id);
    item->flags |= FlItemFlags_scroll_target;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Put the area in item_state so item_state_update fills its bounds from this frame's render command, without the
// scroll_target flag (see ui_track_element in ui.h for the hot/active-arbitration side effect).

void ui_track_element(FlLayoutId layout_id) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return;
    }
    get_or_create_key(self, ui_layout_id_to_clay_id(layout_id));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scoped per-frame wheel delta: the mouse_wheel_delta(id) query. Returns the frame's wheel delta only when this
// id is the hot Scroll Target (consume-takes-all: the topmost hot target wins, ancestors read {0,0}). Reads
// item->state.hot directly, not signal.wheel_delta.
// Per-frame delta only - any cumulative offset is the caller's to accumulate.

FlVec2 ui_mouse_wheel_delta(FlLayoutId layout_id) {
    FlUiCtx* self = g_ui_ctx;
    u32 clay_id = ui_layout_id_to_clay_id(layout_id);
    ItemState* item = hashmap_get(&self->shared->item_state, clay_id);
    if (item == nullptr || !item->state.hot) {
        return (FlVec2) { 0 };
    }
    return (FlVec2) {
        .x = self->input_state.mouse.wheel_x_cached,
        .y = self->input_state.mouse.wheel_y_cached,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlListClipper fl_list_clipper_create(int32_t total_items, float item_height, float scroll_offset_y,
                                     float viewport_height) {
    FlListClipper clipper = { 0 };
    clipper.items_count = total_items;
    clipper.item_height = item_height;
    clipper.scroll_offset_y = scroll_offset_y;
    clipper.viewport_height = viewport_height;

    clipper.items_height = (float)total_items * item_height;

    // A non-positive item height has no valid row window: the divides below would produce inf/NaN, and
    // converting those to int is undefined. Report an empty window instead.
    if (item_height <= 0.0f) {
        return clipper;
    }

    // scroll_offset_y is positive when scrolled down.
    int first = (int)(scroll_offset_y / item_height);
    int last = (int)((scroll_offset_y + viewport_height) / item_height) + 1;

    if (first < 0)
        first = 0;
    if (first > total_items)
        first = total_items;
    if (last < first)
        last = first;
    if (last > total_items)
        last = total_items;

    clipper.display_start = first;
    clipper.display_end = last;

    return clipper;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A spacer's extent is authored logical px, exactly like every FlLayoutSizing the layout seam scales in
// ui_apply_sizing (ui/layout.c) - so it goes through the same scale-and-snap.
static float ui_spacer_extent(float logical) {
    const FlUiCtx* ctx = g_ui_ctx;
    return ui_scale_and_snap(logical, ctx ? ctx->ui_scale : 1.0f);
}

// clang-format off
void ui_spacer(float height_logical) {
    const float height = ui_spacer_extent(height_logical);
    Clay__OpenElement();
    Clay_ElementDeclaration config = { .layout = { .sizing = {
            .width = { .type = CLAY__SIZING_TYPE_GROW },
            .height = { .size = { .minMax = { height, height } },
            .type = CLAY__SIZING_TYPE_FIXED }
            }
        }
    };

    Clay__ConfigureOpenElement(config);
    Clay__CloseElement();
}
// clang-format on

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// clang-format off
void ui_spacer_h(float width_logical) {
    const float width = ui_spacer_extent(width_logical);
    Clay__OpenElement();
    Clay_ElementDeclaration config = { .layout = { .sizing = {
            .width = { .size = { .minMax = { width, width } },
            .type = CLAY__SIZING_TYPE_FIXED },
            .height = { .type = CLAY__SIZING_TYPE_GROW }
            }
        }
    };

    Clay__ConfigureOpenElement(config);
    Clay__CloseElement();
}
// clang-format on

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void retire_stale_items(UiShared* shared) {
    arena_scratch_auto(scratch);

    u32* ids = arena_alloc_array(scratch.arena, u32, hashmap_count(&shared->item_state));
    int remove_count = 0;

    for_each_hashmap(&shared->item_state, key, value) {
        if (value->frame != shared->item_epoch) {
            ids[remove_count++] = *key;
        }
    }

    for_count(i, remove_count) {
        hashmap_remove(&shared->item_state, *ids++);
    }

    shared->item_epoch++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_app_frame_end(FlUiCtx* self) {
    self->shared->item_epoch_app_owned = false;
    retire_stale_items(self->shared);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ItemState* get_or_create_key(FlUiCtx* self, u32 id) {
    ItemState* item = hashmap_get(&self->shared->item_state, id);

    if (item == nullptr) {
        ItemState new_item = { 0 };
        new_item.popup_scope_id = self->current_popup_scope_id;
        new_item.in_active_popup = self->rendering_popup;
        item = hashmap_insert(&self->shared->item_state, id, new_item);
    } else {
        // Re-scope an existing item into the current popup.
        if (self->current_popup_scope_id != 0) {
            item->popup_scope_id = self->current_popup_scope_id;
        }
        item->in_active_popup = self->rendering_popup;
    }

    FL_ASSERT(item != nullptr);
    return item;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_update_end(FlUiCtx* self, float dt) {
    perf_scope_function();

    // Published for the frame just emitted; read by the app after the next frame opens.
    self->input_state.text_input_active = self->text_input_focused_this_frame;
    self->text_input_focused_this_frame = false;
    Clay_RenderCommandArray commands = Clay_EndLayout(dt);
#ifdef FLOWI_UI_INSPECTOR
    // Snapshot the frame here, while Clay's arrays and the strings they point at are still live.
    ui_capture_render_commands(self, &commands);
#endif

    self->should_scroll_to_focused = false;

    ui_hit_test_frame(self, commands, dt);

    // Mouse edges come from the state captured in fl_platform_input_update, before input cleared the flags.
    if (self->mouse_pressed_this_frame && self->hot_id != 0) {
        self->active_id = self->hot_id;
    }

    if (self->mouse_released_this_frame && self->active_id != 0) {
        self->active_id = 0;
    }

    // hot_id is what the next frame reads.
    self->hot_id = self->next_hot_id;

    ui_navigation_update(self);

    ui_scroll_auto_to_focused();

    if (!self->shared->item_epoch_app_owned) {
        retire_stale_items(self->shared);
    }

    // A popup active last frame but not rendered this frame was closed by user code. Modals are exempt: they
    // don't use popup_stack.
    if (self->popup_focus_scope_id != 0 && self->popup_stack_count == 0 && self->modal_scope_id == 0) {
        ui_popup_close_internal(self);
    }

    // Input processing only, no rendering.
    if (self->active_virtual_keyboard) {
        ui_virtual_keyboard_update(self->active_virtual_keyboard, self);
        ui_virtual_keyboard_finish_if_inactive(self);
    }

    // Build the neutral render IR for this frame and hand it to the injected backend. Each element's paint span
    // is spliced inline at its Clay CUSTOM command, so a single Clay→IR pass produces the whole z-ordered
    // stream. Records are 8-aligned so the backend's byte_size walk stays valid.
    FlArena* ir_arena = self->render_frame_allocator;
    fl_arena_align(ir_arena, 8);
    void* ir_start = fl_arena_current_pos(ir_arena);

    fl_clay_to_render_ir(ir_arena, self->render_frame_allocator_state, self->shared->font_cache, commands.internalArray,
                         commands.length);

    size_t ir_bytes = (size_t)((u8*)fl_arena_current_pos(ir_arena) - (u8*)ir_start);

    FlRenderFrame frame = {
        .commands = ir_start,
        .bytes = ir_bytes,
        .global_alpha = self->global_alpha,
        .background = background_controller_snapshot(&self->background),
    };
    // Surface-scoped render: a context with no surface registered renders nothing.
    if (self->surface != nullptr) {
        self->shared->renderer.render(self->shared->renderer.user_data, self->surface, &frame);
    }

    // Frame strings are not legal to mint once the frame closes.
    self->frame_open = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared Frame Update Logic (used by both application and tests)

void ui_frame_end_and_update_input(FlUiCtx* ctx, double dt) {
    ui_update_end(ctx, dt);

    {
        perf_scope("fl_platform_input_update");
        fl_platform_input_update(ctx, dt);
    }
    {
        perf_scope("ui_update_begin");
        ui_update_begin(ctx);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_text(FlString string) {
    // Default text: theme primary colour, everything else falls back inside ui_text_styled_impl.
    const FlThemeColors* theme = ui_get_theme_colors(g_ui_ctx);
    ui_text_styled_impl(string, (FlTextConfig) { .color = theme->text.primary });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_text_styled_impl(FlString string, FlTextConfig config) {
    FlUiCtx* self = g_ui_ctx;

    // Every layout text passes through localization here; widget identity hashes the raw SID.
    string = localization_resolve(self->shared->localization, string);

    FlFontId font_id = config.font_id ? config.font_id : self->active_font;

    // Authored size (config override, else the font's load size) times text_scale: the authored-to-physical
    // crossing for the Clay path; the measure callback and clay_to_render_ir read these fields back.
    const float text_scale = self->text_scale > 0.0f ? self->text_scale : 1.0f;
    u16 authored_size = config.font_size ? config.font_size : font_cache_get_size(self->shared->font_cache, font_id);
    u16 font_size = (u16)max((float)authored_size * text_scale + 0.5f, 1.0f);

    Clay_TextElementConfig clay_config
        = { .fontId = font_id,
            .fontSize = font_size,
            .letterSpacing = (u16)((float)config.letter_spacing * text_scale + 0.5f),
            .lineHeight = (u16)((float)config.line_height * text_scale + 0.5f),
            .wrapMode = config.no_wrap ? CLAY_TEXT_WRAP_NONE : CLAY_TEXT_WRAP_WORDS,
            .textColor = { config.color.r, config.color.g, config.color.b, config.color.a },
            .textAlignment = CLAY_TEXT_ALIGN_LEFT,
            .verticalAlignment = (Clay_TextVerticalAlignment)config.valign,
            .overflow = (Clay_TextOverflow)config.overflow };

    Clay_String clay_string = { .chars = string.data, .length = (u32)string.length };

    Clay__OpenTextElement(clay_string, clay_config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unified item behavior function for flag-based widget composition
//
// One behavior function for every widget family, controlled by flags. Exported in ui_private.h for use by
// custom widgets (like text input).

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlLayoutId resolve_widget_id(FlString fallback, FlString id_override) {
    FlString id_string = id_override.length > 0 ? id_override : fallback;
    return (FlLayoutId) { .name = id_string, .id = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 ui_layout_id_to_clay_id(FlLayoutId layout_id) {
    Clay_ElementId clay_id = Clay__HashString(
        (Clay_String) { .chars = layout_id.name.data, .length = (u32)layout_id.name.length }, layout_id.id);
    return clay_id.id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ItemState* get_item_state(FlLayoutId layout_id) {
    u32 clay_id = ui_layout_id_to_clay_id(layout_id);
    return hashmap_get(&g_ui_ctx->shared->item_state, clay_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlSignal ui_item_behavior(FlLayoutId lay_id, FlItemFlags flags, FlWidgetConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    // The flag says the widget family is focusable; cfg.focusable is the per-instance override on top of it.
    // Only an explicit No subtracts, since Default is also what a zeroed config carries. Yes is honoured by the
    // widget family instead, the only layer that knows what else focus costs it (the scrollbar pairs it with
    // focus_animation).
    if (cfg.focusable == FlFocusable_No) {
        flags &= (FlItemFlags)~FlItemFlags_focusable;
    }

    u32 widget_id = ui_layout_id_to_clay_id(lay_id);

    ItemState* item = get_or_create_key(self, widget_id);
    ANALYZER_ASSUME_NONNULL(item);
    item->flags = flags;

    FlSignal signal = { 0 };

    // With a popup active, input to widgets outside its scope is blocked.
    bool input_blocked = false;
    if (self->popup_focus_scope_id != 0 && item->popup_scope_id != self->popup_focus_scope_id) {
        input_blocked = true;
    }

    if (flags & (FlItemFlags_clickable | FlItemFlags_draggable)) {
        bool mouse_in_bounds = item->state.hot && !input_blocked;
        signal.hovered = mouse_in_bounds;
    }

    // Record which scroll area contains this item - what ui_is_widget_visible and ui_scroll_to_widget read.
    // Keyed on containment, not focusability: an unfocusable item in a scroll area is still clipped by it.
    if (self->scroll_area_stack_depth > 0) {
        ScrollAreaStackEntry* current_scroll = &self->scroll_area_stack[self->scroll_area_stack_depth - 1];
        WidgetScrollInfo info = {
            .scroll_area_id = current_scroll->scroll_area_id,
            .pos_in_content = { 0, 0 }, // queried via Clay_GetElementData when needed
            .size = { 0, 0 },
        };
        hashmap_insert(&self->shared->widget_scroll_map, widget_id, info);
    }

    if (flags & FlItemFlags_focusable) {
        if (self->focus_order != nullptr && self->focus_order_count < self->focus_order_capacity) {
            self->focus_order[self->focus_order_count++] = widget_id;
        }

        // Continued immediate-mode emission must not steal focus back after navigation moves it elsewhere.
        if (cfg.auto_focus && !item->auto_focus_consumed) {
            self->focused_id = widget_id;
            item->auto_focus_consumed = true;
        }

        // Popup auto-focus lands on the first focusable widget in the popup.
        if (self->popup_needs_initial_focus && item->popup_scope_id == self->popup_focus_scope_id) {
            self->focused_id = widget_id;
            self->popup_needs_initial_focus = false;
        }

        signal.focused = item->state.focused && !input_blocked;

        // A widget that just gained focus does not activate on the same frame.
        bool just_gained_focus = item->state.focused && !item->was_focused;
        signal.activated = item->state.focused && self->nav_activate_pressed && !input_blocked && !just_gained_focus;

        if (signal.activated) {
            ui_emit_feedback(self, FlUiFeedbackKind_Activated, 0, widget_id, FlUiFeedbackDir_None);
        }
    }

    if (flags & FlItemFlags_clickable && !input_blocked) {
        bool is_active = (self->active_id == widget_id);
        signal.pressed = signal.hovered && self->mouse_pressed_this_frame;
        signal.released = is_active && self->mouse_released_this_frame;

        signal.clicked = is_active && self->mouse_released_this_frame && signal.hovered;

        // Fast tap: pressed and released in the same frame, where active_id has not been updated yet (that
        // happens in ui_update_end, after widget rendering).
        if (self->mouse_pressed_this_frame && self->mouse_released_this_frame && signal.hovered) {
            signal.clicked = true;
        }

        // Keyboard activation counts as a click.
        if (signal.activated) {
            signal.clicked = true;
        }
    }

    if (flags & FlItemFlags_double_click && !input_blocked) {
        if (signal.clicked) {
            f64 time_since_last = self->input_state.time - item->last_click_time;
            if (time_since_last < UI_MOUSE_DOUBLE_CLICK_TIME) {
                signal.double_clicked = true;
                item->click_count++;
            } else {
                item->click_count = 1;
            }
            item->last_click_time = self->input_state.time;
        }
    }

    // Edge-triggered: fires once on the right-button press over the hovered item.
    if (flags & FlItemFlags_right_click && !input_blocked) {
        if (signal.hovered && self->mouse_buttons_pressed_this_frame[FlMouseButton_Right]) {
            signal.right_clicked = true;
        }
    }

    if (flags & FlItemFlags_draggable && !input_blocked) {
        bool is_active = (self->active_id == widget_id);

        if (signal.hovered && self->mouse_pressed_this_frame) {
            item->is_dragging = true;
            item->drag_start_mouse = (Vec2) { self->input_state.mouse.x, self->input_state.mouse.y };
        }

        // Items that opt into FlItem_DragThreshold only start dragging once the cursor has moved a few pixels
        // from the press point - that's how a plain click (press + release in place) stays distinct from a
        // drag. The distance is the per-button max travelled, tracked by the input layer since the press frame.
        // Items without the flag drag immediately.
        bool past_threshold = !(flags & FlItem_DragThreshold)
                              || self->input_state.mouse.drag_max_distance_sqr[FlMouseButton_Left]
                                     > UI_MOUSE_DRAG_THRESHOLD * UI_MOUSE_DRAG_THRESHOLD;

        if (is_active && item->is_dragging && past_threshold) {
            signal.dragging = true;
            Vec2 current_mouse = { self->input_state.mouse.x, self->input_state.mouse.y };
            signal.drag_delta.x = current_mouse.x - item->drag_start_mouse.x;
            signal.drag_delta.y = current_mouse.y - item->drag_start_mouse.y;
        }

        if (self->mouse_released_this_frame && is_active) {
            item->is_dragging = false;
        }
    }

    if (flags & FlItemFlags_scroll_target && !input_blocked) {
        if (signal.hovered) {
            signal.wheel_delta.x = self->input_state.mouse.wheel_x_cached;
            signal.wheel_delta.y = self->input_state.mouse.wheel_y_cached;
        }
    }

    signal.mouse_pos.x = self->input_state.mouse.x - ITEM_LEFT(item);
    signal.mouse_pos.y = self->input_state.mouse.y - ITEM_TOP(item);

    // The hot/active/focus animations run in item_state_update, off the flags stored above.

    // Kept for test inspection.
    item->last_signal = signal;

    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlSignal ui_widget_behavior(FlLayoutId id, FlWidgetConfig cfg) {
    return ui_item_behavior(id, STANDARD_WIDGET_FLAGS, cfg);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_font_style_register(FlFontId font_id, FlFontStyle style) {
    FL_VALIDATE(g_ui_ctx != nullptr);

    if (style < 0 || style >= FontStyle_Count) {
        log_error("Trying to register a style outside of bounds! skipping");
        return;
    }

    g_ui_ctx->font_styles[style] = font_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_font_style_set(FlFontStyle style) {
    FL_VALIDATE(g_ui_ctx != nullptr);

    if (style < 0 || style >= FontStyle_Count) {
        log_error("Trying to register a style outside of bounds! skipping");
        return;
    }

    g_ui_ctx->active_font = g_ui_ctx->font_styles[style];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_font_set(FlFontId id) {
    FL_VALIDATE(g_ui_ctx != nullptr);
    g_ui_ctx->active_font = id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 ui_get_screen_size(void) {
    FlUiCtx* ctx = g_ui_ctx;
    if (ctx == nullptr) {
        return (FlVec2) { 0, 0 };
    }
    return (FlVec2) { ctx->layout_width, ctx->layout_height };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A font handle carries the authored size; text_scale is multiplied in on use (ui_text_styled_impl, the painter).
// 0 means the default size; 8px is the floor.
static u16 authored_font_size(u16 default_size) {
    u16 size = default_size != 0 ? default_size : 28;
    return size < 8 ? 8 : size;
}

FlFontId ui_font_load(FlVfsMount* mount, FlString relative_path, u16 default_size) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, FONT_ID_INVALID);
    return font_cache_load_font(g_ui_ctx->shared->font_cache, mount, relative_path, authored_font_size(default_size));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The public face of ui_font_load, for a host that loads fonts from its own data mount instead of the asset
// mount fl_font_load resolves against.
FlFontId fl_font_load_mount(FlVfsMount* mount, FlString relative_path, u16 default_size) {
    return ui_font_load(mount, relative_path, default_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_font_load_fallback(FlVfsMount* mount, FlString relative_path) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, false);
    return font_cache_load_fallback(g_ui_ctx->shared->font_cache, mount, relative_path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_font_wait_load(FlFontId font_id) {
    FL_VALIDATE(g_ui_ctx != nullptr);
    font_cache_wait_loaded(g_ui_ctx->shared->font_cache, font_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_font_is_loaded(FlFontId id) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, false);
    return font_cache_get_font_status(g_ui_ctx->shared->font_cache, id) == FontStatus_Ready;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pump I/O until every kicked-off font has its metrics ready (or failed), so setup can load several fonts and
// sync once before the first rendered frame. Returns false if any font failed to load.

bool ui_font_ensure_loaded(void) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, false);
    return font_cache_ensure_all_loaded(g_ui_ctx->shared->font_cache);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFontId fl_font_load_default(u16 default_size) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, FONT_ID_INVALID);
    return font_cache_load_default_font(g_ui_ctx->shared->font_cache, authored_font_size(default_size));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Emit a Clay image element for a prepared neutral image-source descriptor. When fill is set the
// element grows to fill its parent (triggers scaled rendering); otherwise it sizes to w x h pixels.
static void ui_emit_image_element(FlImageSource* source, FlColor color, bool fill, float w, float h) {
    if (fill) {
        CLAY_AUTO_ID({ .backgroundColor = { color.r, color.g, color.b, 255 },
                       .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } },
                       .image = { .imageData = (void*)source } }) {}
    } else {
        CLAY_AUTO_ID({ .backgroundColor = { color.r, color.g, color.b, 255 },
                       .layout = { .sizing = { .width = CLAY_SIZING_FIXED(w), .height = CLAY_SIZING_FIXED(h) } },
                       .image = { .imageData = (void*)source } }) {}
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_image_impl(ImageHandle handle, FlImageOpts opts) {
    ImageResult data = image_get_data(handle);

    ImageStatus status = image_get_status(data);

    if (status != ImageStatus_Ready) {
        return;
    }

    // Clay's imageData slot carries the image-source descriptor; the Clay->IR translator reads it and the
    // backend resolves the handle to pixels on ingest.
    FlImageSource* source = arena_alloc_zero(g_ui_ctx->render_frame_allocator, FlImageSource);
    source->kind = 0;
    source->handle = (FlImageHandle)handle;

    ui_emit_image_element(source, opts.color, opts.fill_container, (float)data.width, (float)data.height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_dynamic_image_impl(FlDynamicImage* image, FlImageOpts opts) {
    if (image == nullptr) {
        return;
    }

    // The descriptor references the live RGBA buffer; the backend deinterleaves it on ingest.
    FlImageSource* source = arena_alloc_zero(g_ui_ctx->render_frame_allocator, FlImageSource);
    source->kind = 1;
    source->dynamic.pixels = image->user_buffer;
    source->dynamic.w = (u32)image->width;
    source->dynamic.h = (u32)image->height;
    source->dynamic.stride = (u32)image->user_stride;

    ui_emit_image_element(source, opts.color, opts.fill_container, (float)image->width, (float)image->height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Scaling API
//
// The core holds zero scaling policy: it stores one absolute UI scale and one absolute text scale,
// multiplies authored logical pixels by them, and computes nothing. Composition (monitor DPI x user zoom, or a
// reference-fit policy) lives in the app layer, which pushes the product here.

void ui_set_scale(float ui_scale, float text_scale) {
    FlUiCtx* ctx = g_ui_ctx;
    if (!ctx) {
        return;
    }

    ctx->ui_scale = ui_scale;
    ctx->text_scale = text_scale;

    // Glyph/SVG pixel sizes key on the UI scale, so the GPU backend regenerates its atlases here (no-op for the
    // software backend). This seam, not a resolution change, is the regen trigger.
    if (ctx->shared->renderer.scale_changed) {
        ctx->shared->renderer.scale_changed(ctx->shared->renderer.user_data, ui_scale);
    }

    log_info("UI scale set to ui_scale=%.3f, text_scale=%.3f", ui_scale, text_scale);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_set_display_resolution(int width, int height) {
    FlUiCtx* ctx = g_ui_ctx;
    if (!ctx) {
        return;
    }

    // Layout area only - no scale side effect. Scale is set independently via ui_set_scale.
    ctx->layout_width = (float)width;
    ctx->layout_height = (float)height;

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)width, .height = (float)height });

    // The renderer's framebuffer is surface-scoped: resize only when this window has a surface.
    if (ctx->shared->renderer.change_resolution && ctx->surface != nullptr) {
        ctx->shared->renderer.change_resolution(ctx->shared->renderer.user_data, ctx->surface, width, height);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ui_get_scale(void) {
    FlUiCtx* ctx = g_ui_ctx;
    return ctx ? ctx->ui_scale : 1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ui_get_text_scale(void) {
    FlUiCtx* ctx = g_ui_ctx;
    return ctx ? ctx->text_scale : 1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background Image API

void ui_background_set(ImageHandle handle) {
    FlUiCtx* ctx = g_ui_ctx;
    if (!ctx)
        return;
    background_controller_set(&ctx->background, handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_background_set_alpha(u8 alpha) {
    FlUiCtx* ctx = g_ui_ctx;
    if (!ctx)
        return;
    background_controller_set_alpha(&ctx->background, alpha);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_background_set_clear_color(u8 r, u8 g, u8 b) {
    FlUiCtx* ctx = g_ui_ctx;
    if (!ctx)
        return;
    background_controller_set_clear_color(&ctx->background, r, g, b);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background blend API

void ui_background_set_blend(ImageHandle prev, ImageHandle current, u8 t) {
    FlUiCtx* ctx = g_ui_ctx;
    if (!ctx)
        return;
    background_controller_set_blend(&ctx->background, prev, current, t);
}
