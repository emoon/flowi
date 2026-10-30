#pragma once

// Shared fixtures and the include set for the unit_tests translation units
// (unit_tests.c plus unit_tests_<area>.c). Every test TU includes this header.

#include "app/app.h"
#include <platform/window_backend.h>
#include <core/arena.h>
#include <core/core.h>
#include <core/cpu_topology.h>
#include <core/env_expand.h>
#include <core/memory.h>
#include <core/hashmap.h>
#include <core/string.h>
#include <core/hash.h>
#include <core/sprintf.h>
#include <core/log.h>
#include <core/mcmpq.h>
#include <core/os/os.h>
#include <core/pool_allocator.h>
#include <core/tlsf.h>
#include <core/heap.h>
#include <flowi/core/jobs.h>
#include <core/perf_counters.h>
#include "external/clay/clay.h"
#include "external/utest/utest.h"
#include <render/gpu/gpu_glyph_atlas.h>
#include <render/gpu/gpu_glyph_lower.h>
#include <render/gpu/gpu_ir_to_draw_cmd.h>
#include <render/gpu/gpu_orchestrator.h>
#include <render/gpu/gpu_prepare_core.h>
#include <render/gpu/gpu_svg_atlas.h>
#include <render/gpu/gpu_svg_color_lower.h>
#include <render/gpu/gpu_svg_translate.h>
#include <render/software/software_renderer.h>
#include <render/software/sr_command.h>
#include <render/software/sw_glyph_cache.h>
#include <render/software/sr_image_convert.h>
#include <render/software/sr_ir_to_render_command.h>
#include <ui/ui_theme.h>
#include "ui/background.h"
#include "ui/clay_to_render_ir.h"
#include "ui/color.h"
#include "ui/color_oklch.h"
#include "ui/dock.h"
#include "ui/dock/dock_drag.h"
#include "ui/dock/dock_layout.h"
#include "ui/dock/dock_persist.h"
#include "ui/dock/dock_shared.h"
#include "ui/dock/dock_splitter.h"
#include "ui/dock/dock_tree.h"
#include "ui/dock/dock_workspace.h"
#include <flowi/ui/dock.h>
#include <core/json.h>
#include "core/file_watcher.h"
#include "core/stats.h"
#include "core/file_watcher_internal.h"
#include "core/app_identity.h"
#include "ui/font_cache.h"
#include <flowi/ui/cursor.h>
#include <flowi/ui/feedback.h>
#include <flowi/ui/format.h>
#include <flowi/ui/ui_input.h>
#include <flowi/ui/frame.h>
#include <flowi/ui/focused.h>
#include <flowi/ui/nav.h>
#include <flowi/ui/list_clipper.h>
#include <flowi/ui/theme.h>
#include <flowi/ui/inspector.h>
#include <flowi/ui/button.h>
#include <flowi/ui/checkbox.h>
#include <flowi/ui/chip.h>
#include <flowi/ui/dropdown.h>
#include <flowi/ui/item_behavior.h>
#include <flowi/ui/dynamic_image.h>
#include <flowi/ui/link.h>
#include <flowi/ui/pill.h>
#include <flowi/ui/progress_bar.h>
#include <flowi/ui/radio_group.h>
#include <flowi/ui/scrollbar.h>
#include <flowi/ui/spacer.h>
#include <flowi/ui/switch.h>
#include <flowi/ui/tab_bar.h>
#include <flowi/ui/text_input.h>
#include <flowi/ui/viewport3d.h>
#include "ui/image.h"
#include "ui/image_cache.h"
#include "ui/image_decode.h"
#include "ui/image_private.h"
#include "ui/image_registry.h"
#include "ui/image_system.h"
#include <flowi/ui/font.h>
#include <flowi/ui/icon.h>
#include "ui/layout.h"
#include <flowi/ui/layout_area.h>
#include <flowi/ui/modal.h>
#include "ui/painter.h"
#include "ui/reference_fit.h"
#include <ui/render_backend.h>
#include "ui/render_ir_build.h"
#include "ui/scroll_core.h"
#include "ui/ui.h"
#include "ui/ui_list.h"
#include "ui/ui_menu.h"
#include "ui/ui_message_box.h"
#include "ui/ui_private.h"
#include "ui/ui_scroll.h"
#include "ui/ui_selection.h"
#include "ui/ui_widget_internal.h"
#include "ui/ui_text_buffer.h"
#include "ui/ui_text_input.h"
#include "ui/ui_tooltip.h"
#include "ui/ui_variant_helpers.h"
#include "ui/ui_virtual_keyboard.h"
#include "ui/vector_image.h"
#include "vfs/vfs.h"
#include "vfs/drivers/localfs.h"
#include "vfs/vfs_private.h"
#include <core/string_allocator.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/glyph_test_fonts.h"
#include "render_seam.h"

// Arena from fl_init, shared by all tests (set up once in main below).

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vfs.read_through_generated_plugin - drives the api_gen-generated FlVfsPlugin driver vtable
// (<flowi/vfs/vfs_plugin.h>) end-to-end through a hermetic in-memory driver, no disk I/O. The driver mirrors
// LocalFS's handle semantics (stateless instance, self-describing handles), backed by one in-memory file.

#define STUB_VFS_FILE "hello.txt"

// One open resource in the in-memory driver: either the mount-root directory or the single file.
typedef struct StubVfsHandle {
    bool is_dir;
    int64_t offset; // read cursor for the file
} StubVfsHandle;

// When set, get_size for the file reports one byte over the VFS read-all cap (512 MB), driving
// read-all's over-cap hard-error branch. The check precedes the allocation, so no 512 MB buffer
// is ever allocated.
#define STUB_VFS_OVERSIZE ((int64_t)512 * 1024 * 1024 + 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render: shared fixture - software renderer + UI context + VFS + image system + font cache.
//
// Created lazily by the first render_fixture_get() in a case (fl_vfs_init / ui_create / image_create_system), and
// released by render_fixture_teardown. The runner releases it around every case, so a case always starts from a
// fresh fixture.

typedef struct RenderFixture {
    bool created;
    FlArena* arena; // owns everything the fixture allocates, released whole by render_fixture_teardown
    FlRenderBackend backend;
    FlUiCtx* ctx;
    void (*render)(void* user_data, FlSurface* surface, const FlRenderFrame* frame);
    u8 submitted_global_alpha;
    FlBackground submitted_background;
    // The first border / text-run record of the last frame. The frame's IR arena is rewound once the frame is
    // done, so this is the only view a case gets of what the layout -> Clay -> IR path carried.
    bool border_found;
    FlCmdBorder submitted_border;
    bool text_run_found;
    FlCmdTextRun submitted_text_run;
    FlGlyph text_run_glyphs[4]; // leading glyphs of that run: pen deltas are the only view of letter spacing
    u32 text_run_glyph_count;   // how many of the above are populated (the run may carry more)
    bool owns_vfs;
    bool owns_image_system;
    FlFontCache* font_cache; // the cache bound to ctx, so a case can load a font into the one the frame measures with
} RenderFixture;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixtures and helpers defined in unit_tests_common.c, used by more than one area's TU.

extern FlArena* s_arena;
#if defined(__linux__)
u64 count_proc_self_maps(void);
#endif
extern const uint8_t k_stub_vfs_data[9];
FlVfsDriverError stub_vfs_ok(void);
FlVfsDriverError stub_vfs_not_found(void);
extern _Atomic int s_stub_vfs_alloc_count;
extern _Atomic int s_stub_vfs_close_count;
extern _Atomic int s_stub_vfs_read_count;
extern bool s_stub_vfs_open_as_file;
extern bool s_stub_vfs_short_read;
// Gate for stub_vfs_read, off unless a case arms it: an armed read announces itself through
// s_stub_vfs_read_entered and then blocks until s_stub_vfs_read_release, which lets a case hold a VFS
// operation open across calls it makes on another thread. Disarm and release before leaving the case.
extern _Atomic bool s_stub_vfs_read_gate_armed;
extern _Atomic bool s_stub_vfs_read_entered;
extern _Atomic bool s_stub_vfs_read_release;
extern bool s_stub_vfs_oversize;
extern const u8* s_stub_vfs_override_data;
extern size_t s_stub_vfs_override_size;
const u8* stub_vfs_data(size_t* size);
StubVfsHandle* stub_vfs_alloc(bool is_dir);
bool stub_vfs_is_file(FlString path);
bool stub_vfs_is_stub_root(FlString path);
bool stub_vfs_supports_path(FlString path);
bool stub_vfs_can_open(FlString path);
FlVfsOpenResult stub_vfs_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                              FlVfsMountOptions mount_options);
bool stub_vfs_is_directory(void* instance, void* handle);
FlVfsSizeResult stub_vfs_get_size(void* instance, void* handle, FlString relative_path);
FlVfsReadResult stub_vfs_read(void* instance, void* handle, FlString relative_path, uint8_t* buffer, int64_t size);
void stub_vfs_list_directory(void* instance, void* handle, FlString relative_path, FlVfsListingCallback callback,
                             void* user_data);
void stub_vfs_close(void* instance, void* handle);
FlVfsDriverCapabilities stub_vfs_get_capabilities(void);
extern FlVfsPlugin s_stub_vfs_plugin;
extern RenderFixture s_render;
void render_fixture_capture(void* user_data, FlSurface* surface, const FlRenderFrame* frame);
FlUiCtx* render_fixture_get(void);
void render_fixture_teardown(void);
// Releases the render fixture and the VFS and image system singletons, so the calling case starts from none.
void process_singletons_reset(void);
// vfs_register_driver for a plugin the live VFS has not registered yet, so repeated cases do not stack copies.
void vfs_test_register_driver(const FlVfsPlugin* plugin);
void scene_many_elements(FlUiCtx* ctx, int n);
bool color_eq(FlColor a, FlColor b);
bool slot_override_color_eq(UiColor4 got, FlColor want);
int render_cmd_collect(UiRenderCommandType type, UiRenderCommand* out, int max);
bool tab_color_eq(UiColor4 got, FlColor want);
void canvas_fixture_reset(FlUiCtx* ctx);
bool focus_ring_drawn(void);
#if defined(__linux__)
int count_open_fds(void);
#endif
