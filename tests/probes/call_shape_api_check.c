///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only guard for the generated call macros. With the implementation define unset, a trailing *Config / *Opts
// argument is written as a designated-initializer list, and every other trailing argument (handle, id, color, event,
// vector) is passed as a plain expression.

#ifdef FL_API_IMPLEMENTATION
#undef FL_API_IMPLEMENTATION
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <core/string.h>
#include <flowi/core/jobs.h>
#include <flowi/platform/input.h>
#include <flowi/ui/button.h>
#include <flowi/ui/cursor.h>
#include <flowi/ui/font.h>
#include <flowi/ui/image.h>
#include <flowi/ui/painter.h>
#include <flowi/ui/ui_input.h>
#include <flowi/ui/ui_menu.h>
#include <flowi/ui/viewport3d.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Never called; it only has to compile.
static void call_shape_api_check_compile_only(FlColor color, FlLayoutId id, FlJobHandle job, FlImageHandle image,
                                              FlFontId font, FlMeshHandle mesh, FlCursor cursor, FlKeyEvent key,
                                              FlElementId element, FlString text) {
    fl_painter_rect(0, 0, 1, 1, color);
    fl_painter_rect_rounded(0, 0, 1, 1, 2, color);
    fl_painter_hline(0, 0, 4, color);
    fl_input_mouse_position(id);
    fl_input_get_element_state(id);
    fl_input_get_element_state_by_id(element);
    fl_input_push_key_event(key);
    fl_viewport3d_begin(id);
    fl_jobs_wait(job);
    fl_painter_image(0, 0, 1, 1, image, color);
    fl_font_set(font);
    fl_menu_open(id, (FlVec2) { 1.0f, 2.0f });
    fl_menu_close(id);
    fl_menu_is_open(id);
    fl_menubar_is_open(id);
    fl_mesh_destroy(mesh);
    fl_cursor_set(cursor);

    fl_button(text, .color = color);
    fl_image(image, (FlImageOpts) { .fill_container = true });
}

void (*call_shape_api_check_anchor)(FlColor, FlLayoutId, FlJobHandle, FlImageHandle, FlFontId, FlMeshHandle, FlCursor,
                                    FlKeyEvent, FlElementId, FlString) = call_shape_api_check_compile_only;
