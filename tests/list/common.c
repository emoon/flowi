// Shared fixtures for the unit_tests_list*.c translation units.

#include "list/common.h"

void list_model_init(ListModel* model, FlLayoutId list, u32 count, f32 viewport_h) {
    *model = (ListModel) {
        .list = list,
        .config = { .row_height = 24.0f, .gap = 4.0f },
        .count = count,
        .viewport_h = viewport_h,
    };
    for (u32 i = 0; i < count; i++) {
        model->ids[i] = (u64)(i + 1);
    }
}

void list_model_scale_ids(ListModel* model, u64 factor) {
    for (u32 i = 0; i < model->count; i++) {
        model->ids[i] *= factor;
    }
}

static FlListResult list_emit_rows(const ListModel* model, const ListFrameOpts* opts) {
    if (opts->request_focus) {
        fl_list_request_focus(model->list, opts->focus_seed);
    }
    FlListRange range = fl_list_begin(model->list, model->ids, model->count, model->set, model->config);
    if (opts->out_range != nullptr) {
        *opts->out_range = range;
    }
    for (u32 i = range.start; i < range.end; i++) {
        FlSignal signal = fl_list_item_begin(model->ids[i]);
        if (opts->out_signals != nullptr) {
            opts->out_signals[i] = signal;
        }
        ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
        fl_list_item_end();
    }
    return fl_list_end();
}

FlListResult list_emit_frame(FlUiCtx* ctx, const ListModel* model, const ListFrameOpts* opts) {
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    FlListResult result = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(model->viewport_h) },
                   .direction = FlLayoutDirection_TopToBottom) {
        result = list_emit_rows(model, opts);
    }
    if (opts->out_nav != nullptr) {
        *opts->out_nav = fl_get_nav_frame(); // a direction the List consumed reads back false
    }
    if (opts->out_global_focus != nullptr) {
        *opts->out_global_focus = ctx->focused_id;
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return result;
}

void list_prepare_surface(FlUiCtx* ctx) {
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
}

void list_park_cursor(FlUiCtx* ctx) {
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
}

void list_release_mouse(FlUiCtx* ctx) {
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = false;
}

void list_pointer_set(FlUiCtx* ctx, ListPointer pointer) {
    FlInputState* in = &ctx->input_state;
    ui_input_set_mouse_position(ctx, pointer.x, pointer.y);
    in->mouse.buttons[FlMouseButton_Left] = pointer.held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pointer.pressed;
    in->mouse.buttons_released[FlMouseButton_Left] = pointer.released;
}

bool list_app_open(ListApp* app, FlString title) {
    FlAppSettings settings = { .window_title = title, .width = 400, .height = 400, .headless = true };
    app->app = fl_app_create(&settings);
    app->ctx = g_ui_ctx;
    if (app->app == nullptr) {
        return false;
    }
    app->ctx->ui_scale = 1.0f;
    app->ctx->layout_width = 400.0f;
    app->ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });
    return true;
}

void list_app_close(ListApp* app) {
    fl_app_destroy(app->app);
}

FlListRange list_boxed_step(FlUiCtx* ctx, const ListModel* model, const ListBoxedStep* step) {
    list_pointer_set(ctx, step->pointer);
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->input_state.keyboard.modifiers = step->modifiers;
    ctx->nav_frame.nav_down = step->nav_down;

    FlListRange range = { 0, 0, 0 };
    const ListFrameOpts opts = { .out_range = &range };
    ui_update_begin(ctx);
    ui_layout_area_id(step->root, .sizing = { sizing_fixed(300), sizing_fixed(120) },
                      .direction = LayoutDirection_TopToBottom) {
        const FlListResult result = list_emit_rows(model, &opts);
        if (step->out_result != nullptr) {
            *step->out_result = result;
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_down = false; // the injected edge must not reach a later frame
    return range;
}

void list_sel_only_hot(FlUiCtx* ctx, const ListModel* model, int hot_pos) {
    for (u32 p = 0; p < model->count; p++) {
        const FlLayoutId row = { .name = model->list.name, .id = (i32)(p + 1) };
        ItemState* it = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(row));
        if (it != nullptr) {
            it->state.hot = (hot_pos >= 0 && (u32)hot_pos == p) ? 1 : 0;
        }
    }
}

FlListResult list_sel_frame(FlUiCtx* ctx, const ListModel* model, SelInput input) {
    list_park_cursor(ctx);
    ctx->input_state.keyboard.modifiers = input.modifiers;
    ctx->keys_pressed_this_frame[FlKey_A] = input.key_a;
    ctx->mouse_pressed_this_frame = input.click;
    ctx->mouse_released_this_frame = input.click;
    return list_emit_frame(ctx, model, &(ListFrameOpts) { .out_signals = input.out_signals });
}

FlListRange list_sel_setup(FlUiCtx* ctx, const ListModel* model) {
    list_prepare_surface(ctx);
    list_sel_frame(ctx, model, (SelInput) { 0 });
    list_sel_frame(ctx, model, (SelInput) { 0 });

    FlListRange range = { 0, 0, 0 };
    list_emit_frame(ctx, model, &(ListFrameOpts) { .out_range = &range });
    return range;
}

FlListResult list_sel_click_row(FlUiCtx* ctx, const ListModel* model, int pos, u32 modifiers) {
    list_sel_only_hot(ctx, model, pos);
    return list_sel_frame(ctx, model, (SelInput) { .modifiers = modifiers, .click = true });
}
