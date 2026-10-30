#pragma once

#include <core/string.h>
#include <core/types.h>
#include <flowi/ui/scroll.h>        // @generated FlScrollAreaConfig (api_gen)
#include <flowi/ui/signal.h>        // @generated FlSignal (api_gen)
#include <flowi/ui/ui_scroll.h>     // @generated fl_scroll_area_begin / fl_scroll_area_end prototypes (api_gen)
#include <flowi/ui/widget_config.h> // @generated FlScrollbarConfig (api_gen)
#include <ui/ui_types.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Scroll Area - Reusable scrolling container with automatic state management
//
// Usage:
//   ui_scroll_area_id(ID("my_scroll"),
//       .sizing = { sizing_grow(0), sizing_grow(0) },
//       .direction = LayoutDirection_TopToBottom,
//       .show_scrollbar = true,
//       .smooth_scroll = true)
//   {
//       // Content here
//   }
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal scroll state (stored per ID in FlUiCtx)

typedef struct ScrollAreaState {
    FlVec2 scroll_offset; // Current scroll position (pixels)
    FlVec2 scroll_target; // Target for smooth scrolling
    FlVec2 content_size;
    FlVec2 viewport_size;
    u64 last_frame; // Frame when last updated (for cleanup)
    u32 parent_id;  // Enclosing scroll area's element id, 0 at the top level
    bool is_hovered;
} ScrollAreaState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scroll-to-widget options

typedef enum FlScrollAlignment {
    FlScrollAlign_Nearest = 0, // Minimum scroll to make visible (default)
    FlScrollAlign_Top,         // Align widget to top of viewport
    FlScrollAlign_Center,      // Center widget in viewport
    FlScrollAlign_Bottom,      // Align widget to bottom of viewport
} FlScrollAlignment;

typedef struct FlScrollToOptions {
    float margin;            // Extra space around widget (default: 20.0f)
    bool immediate;          // Skip smooth scroll animation
    FlScrollAlignment align; // Alignment mode (default: Nearest)
} FlScrollToOptions;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scroll area stack entry (one per open scroll area)

#define SCROLL_AREA_STACK_MAX_DEPTH 8

typedef struct ScrollAreaStackEntry {
    u32 scroll_area_id;
    ScrollAreaState* state;
    FlVec2 viewport_pos; // Screen position of viewport
    FlVec2 viewport_size;
    FlScrollAreaConfig config;
} ScrollAreaStackEntry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget scroll info (for tracking widget position within scroll area)

typedef struct WidgetScrollInfo {
    u32 scroll_area_id;    // Containing scroll area (0 if none)
    FlVec2 pos_in_content; // Position relative to scroll content origin
    FlVec2 size;
} WidgetScrollInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API - Widget visibility and scroll-to-widget

// Returns true if the widget is not in a scroll area, or if it is visible in the viewport.
bool ui_is_widget_visible(FlLayoutId widget_id);

// Scroll to make a widget visible (with default margin of 20px)
void ui_scroll_to_widget(FlLayoutId widget_id);

void ui_scroll_to_widget_ex(FlLayoutId widget_id, FlScrollToOptions opts);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scrollbar widget - returns signal with value changes tracked by signal.changed

FlSignal ui_scrollbar_impl(float* value, FlScrollbarConfig cfg);
#define ui_scrollbar(value, ...) ui_scrollbar_impl(value, (FlScrollbarConfig) { __VA_ARGS__ })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal API (used by ui.c)

// Auto-scroll to focused widget if it's off-screen (called at end of frame)
void ui_scroll_auto_to_focused(void);

// Test-only: force scroll-state creation to report allocation failure, driving fl_scroll_area_begin down its
// no-state fallback path. Existing states are unaffected.
void ui_scroll_test_force_state_alloc_failure(bool enable);
