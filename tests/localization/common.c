// Shared localization fixture helpers for the unit_tests_localization*.c translation units

#include "localization/common.h"

FlVfsMount* localization_mount(void) {
    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(FL_S(FLOWI_TEST_DATA_DIR "/localization"));
    if (mount.status != FlVfsMountErrorStatus_Success) {
        return nullptr;
    }
    vfs_wait_all();
    return mount.mount;
}

// Runs frame boundaries until nothing is in flight, as an app tick loop does. Every read runs on a VFS worker, so
// a case that wants a loaded table has to tick until it arrives.
void localization_settle(struct Localization* loc) {
    for (u32 i = 0; i < 100000; i++) {
        localization_frame_begin(loc);
        if (!localization_is_loading(loc)) {
            return;
        }
        fl_os_sleep_us(200);
    }
}

// Initializes localization against the example data and ticks until the default table is loaded.
FlUiCtx* localization_fixture(FlVfsMount** out_mount, FlString manifest_path) {
    FlUiCtx* ctx = render_fixture_get();
    if (ctx == nullptr) {
        return nullptr;
    }
    *out_mount = localization_mount();
    if (*out_mount == nullptr) {
        return nullptr;
    }
    fl_localization_init(*out_mount, manifest_path, false);
    localization_settle(ctx->shared->localization);
    return ctx;
}

void localization_fixture_release(FlUiCtx* ctx, FlVfsMount* mount) {
    localization_destroy(ctx->shared->localization);
    ctx->shared->localization = nullptr;
    if (mount != nullptr) {
        fl_vfs_mount_close(mount);
    }
}

// Emits one frame holding label as text inside an element identified by id_source, then serializes the tree.
// Returns the size the snapshot needed, so a caller can tell a truncated buffer from an absent string.
int32_t localization_emit(FlUiCtx* ctx, FlString id_source, FlString label, SnapshotBuffer out) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        // The background colour makes the area emit a render command, which the inspector reads.
        ui_layout_area_id(resolve_widget_id(id_source, string_empty()),
                          .sizing = { sizing_fixed(200), sizing_fixed(40) }, .color = ui_color(40, 200, 60, 255)) {
            ui_text(label);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    return fl_inspector_snapshot(out.data, out.size);
}
