///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Standalone compile guard for the hand-written public block-scoped layout macros (<flowi/ui/layout_area.h>).
//
// This TU links nothing. It proves at compile time that the header is self-contained valid C given only FlString,
// and that FL_ID / FL_ID_INT and the layout, floating and scroll block macros expand to valid C against the public
// types alone.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FlString - includer-provided base type api_gen does not author.
#include <core/string.h>

#include <flowi/ui/layout_area.h>

// The internal sizing_* helper macros are not available in a no-internals TU, so spell the axes from public types.
#define FL__CHECK_GROW ((FlLayoutSizingAxis) { .type = FlLayoutSizingType_Grow })
#define FL__CHECK_FIXED(px) \
    ((FlLayoutSizingAxis) { .size = { .min_max = { (px), (px) } }, .type = FlLayoutSizingType_Fixed })

// Never called: linking it would need a live Clay context. Compiling it is the whole point.
static void fl_layout_area_api_check_compile_only(void) {
    FlLayoutId id = FL_ID("compile_check");
    FlLayoutId seeded = FL_ID_INT("compile_check", 7);
    (void)id;
    (void)seeded;

    fl_layout_area(.sizing = { .width = FL__CHECK_GROW, .height = FL__CHECK_GROW }) {
        fl_layout_area_id(FL_ID("inner"), .sizing = { .width = FL__CHECK_FIXED(120), .height = FL__CHECK_GROW }) {
            // Nested ≥2 deep: the inner loop completes inside the outer body, exercising the shared-latch nesting.
        }
    }

    FlLayoutConfig cfg = { .sizing = { .width = FL__CHECK_GROW, .height = FL__CHECK_GROW } };
    fl_layout_area_cfg(cfg) {}

    fl_floating_area(.attach_to = FlFloatingAttachTo_Root,
                     .sizing = { .width = FL__CHECK_FIXED(120), .height = FL__CHECK_FIXED(120) }) {}
    fl_floating_area_id(FL_ID("dialog"), .attach_to = FlFloatingAttachTo_Root,
                        .sizing = { .width = FL__CHECK_FIXED(80), .height = FL__CHECK_FIXED(80) }) {}
    fl_scroll_area_id(FL_ID("scroll"), .sizing = { .width = FL__CHECK_GROW, .height = FL__CHECK_GROW },
                      .show_scrollbar = true) {}
}

// Reference it from a (still never-called) anchor so -Wunused-function stays quiet without changing the above.
void (*fl_layout_area_api_check_anchor)(void) = fl_layout_area_api_check_compile_only;
