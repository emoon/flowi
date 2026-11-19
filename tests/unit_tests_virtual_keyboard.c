// UI virtual keyboard tests

#include "unit_tests_common.h"
#include <math.h>

// The virtual keyboard lives across many frames while the caller's title and allowed_chars storage may not, so
// ui_virtual_keyboard_create copies the caller's strings into its own arena, which is rewound for each cycle.

UTEST(virtual_keyboard, create_owns_strings_and_does_not_leak) {
    FlArena* arena = arena_new();
    ASSERT_TRUE(arena != nullptr);

    // Caller strings that are not static, in mutable stack buffers.
    char title_buf[] = "Enter name";
    char chars_buf[] = "abc";
    VirtualKeyboardConfig config = { .max_length = 0,
                                     .allowed_chars = string_from_cstr_len(chars_buf, sizeof(chars_buf) - 1),
                                     .title = string_from_cstr_len(title_buf, sizeof(title_buf) - 1),
                                     .initial_text = string_empty(),
                                     .show_character_count = false,
                                     .initial_page = VK_PAGE_LOWERCASE };

    VirtualKeyboard* vk = ui_virtual_keyboard_create(arena, config);
    // The strings are copies and do not point at the caller's buffers.
    ASSERT_TRUE(vk->config.title.data != title_buf);
    ASSERT_TRUE(vk->config.allowed_chars.data != chars_buf);
    ASSERT_EQ((u64)vk->config.title.length, (u64)(sizeof(title_buf) - 1));
    ASSERT_EQ((u64)vk->config.allowed_chars.length, (u64)(sizeof(chars_buf) - 1));

    // Overwrite the caller's buffers. The keyboard's copies are unaffected.
    memory_set(title_buf, sizeof(title_buf), 'X', sizeof(title_buf));
    memory_set(chars_buf, sizeof(chars_buf), 'X', sizeof(chars_buf));
    ASSERT_EQ(memory_compare(vk->config.title.data, "Enter name", 10), 0);
    ASSERT_EQ(memory_compare(vk->config.allowed_chars.data, "abc", 3), 0);

    // Static (FL_S("...")) strings are already stable storage, so fl_string_copy returns them unchanged without
    // allocating.
    VirtualKeyboardConfig static_cfg = config;
    static_cfg.title = FL_S("Static title");
    VirtualKeyboard* vk_static = ui_virtual_keyboard_create(arena, static_cfg);
    ASSERT_TRUE(vk_static->config.title.data == FL_S("Static title").data);

    // With a rewind before each create, as the focus-gain path does, the arena position after N cycles equals the
    // position after a single create.
    fl_arena_rewind(arena);
    ui_virtual_keyboard_create(arena, config);
    void* pos_after_one = fl_arena_current_pos(arena);
    for (int i = 0; i < 50; ++i) {
        fl_arena_rewind(arena);
        ui_virtual_keyboard_create(arena, config);
    }
    ASSERT_TRUE(fl_arena_current_pos(arena) == pos_after_one);

    fl_arena_destroy(arena);
}

typedef struct VkFocusRig {
    FlUiCtx* ctx;
    FlTextInputState state;
    u32 input_clay_id;
} VkFocusRig;

static void vk_focus_input_frame(VkFocusRig* rig) {
    rig->ctx->mouse_pressed_this_frame = false;
    rig->ctx->mouse_released_this_frame = false;
    ui_update_begin(rig->ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_text_input(FL_S("vk_focus_input"), &rig->state, (FlTextInputConfig) { 0 });
    }
    ui_update_end(rig->ctx, 1.0f / 60.0f);
}

// Focus takes effect one frame after focused_id is set (item->state.focused is written from focused_id in the
// command pass), and focus_gained needs an unfocused frame before it, so a short bounded loop gets there.
static void vk_focus_until_keyboard_spawns(VkFocusRig* rig) {
    rig->ctx->focused_id = rig->input_clay_id;
    for (int i = 0; i < 8 && rig->ctx->active_virtual_keyboard == nullptr; i++) {
        vk_focus_input_frame(rig);
    }
}

// Blurs until the input reports unfocused.
static void vk_blur_until_unfocused(VkFocusRig* rig) {
    rig->ctx->focused_id = 0;
    for (int i = 0; i < 8 && rig->state.was_focused_last_frame; i++) {
        vk_focus_input_frame(rig);
    }
}

static bool pointer_within(const void* ptr, const void* low, const void* high) {
    return (const u8*)ptr >= (const u8*)low && (const u8*)ptr < (const u8*)high;
}

// Runs a real ui_text_input through the focus-gain path and checks that the keyboard is allocated from
// ctx->vk_arena and not the persistent arena, and that repeated focus-gain cycles do not grow it.
UTEST(virtual_keyboard, focus_gain_uses_vk_arena_and_does_not_grow) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ctx->vk_arena != nullptr, "ui_create wired ctx->vk_arena");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);

    // With no physical keyboard, a text input that gains focus opens the virtual keyboard.
    ctx->physical_keyboard_available = false;
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;

    VkFocusRig rig
        = { .ctx = ctx,
            .input_clay_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("vk_focus_input"), .id = 0 }) };

    vk_focus_until_keyboard_spawns(&rig);
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "focus-gain with no physical keyboard spawns a VK");

    // The keyboard is in vk_arena and not the persistent arena.
    ASSERT_TRUE_MSG(
        pointer_within(ctx->active_virtual_keyboard, ctx->vk_arena->ptr, fl_arena_current_pos(ctx->vk_arena)),
        "VK allocated from ctx->vk_arena, not ctx->persistant");

    void* pos_cycle1 = fl_arena_current_pos(ctx->vk_arena);

    // Dismiss and run a second full focus-gain cycle. The fl_arena_rewind per cycle keeps the growth at zero.
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;
    vk_blur_until_unfocused(&rig);
    vk_focus_until_keyboard_spawns(&rig);
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "second focus-gain re-spawns a VK");
    ASSERT_TRUE_MSG(fl_arena_current_pos(ctx->vk_arena) == pos_cycle1, "vk_arena does not grow across focus cycles");
}

// A keyboard that closed itself is retired by the close-out code that ui_update_end and
// ui_virtual_keyboard_update_and_render both run. A submit commits its buffer to the target input and a cancel
// discards it. Both clear the two context slots.

UTEST(virtual_keyboard, close_out_commits_on_submit_and_discards_on_cancel) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    ctx->physical_keyboard_available = false;
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;

    FlTextInputState state = { 0 };
    const u32 ti_clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("vk_closeout_input"), .id = 0 });

#define VK_TI_FRAME()                                                                    \
    do {                                                                                 \
        ctx->mouse_pressed_this_frame = false;                                           \
        ctx->mouse_released_this_frame = false;                                          \
        ui_update_begin(ctx);                                                            \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                     \
            ui_text_input(FL_S("vk_closeout_input"), &state, (FlTextInputConfig) { 0 }); \
        }                                                                                \
        ui_update_end(ctx, dt);                                                          \
    } while (0)

    // Submit: the keyboard's buffer is written to the target input.
    ctx->focused_id = ti_clay;
    for (int i = 0; i < 8 && ctx->active_virtual_keyboard == nullptr; i++) {
        VK_TI_FRAME();
    }
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "focus-gain spawns a VK to close out");

    VirtualKeyboard* vk = ctx->active_virtual_keyboard;
    ASSERT_TRUE_MSG(vk->buffer_capacity >= 6, "VK buffer holds the test string");
    memcpy(vk->buffer, "hello", 5);
    vk->buffer_length = 5;
    vk->submitted = true;
    vk->cancelled = false;
    vk->active = false; // the keyboard closed itself this frame

    VK_TI_FRAME();

    FlString committed = ui_text_input_get_text(&state);
    ASSERT_EQ_MSG(5u, (u32)committed.length, "submitted text length reaches the input");
    ASSERT_TRUE_MSG(memcmp(committed.data, "hello", 5) == 0, "submitted text content reaches the input");
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard == nullptr, "submit clears active_virtual_keyboard");
    ASSERT_TRUE_MSG(ctx->vk_target_text_input == nullptr, "submit clears vk_target_text_input");

    // Cancel: the input keeps the text the submit above committed.
    ctx->focused_id = 0;
    for (int i = 0; i < 8 && state.was_focused_last_frame; i++) {
        VK_TI_FRAME();
    }
    ctx->focused_id = ti_clay;
    for (int i = 0; i < 8 && ctx->active_virtual_keyboard == nullptr; i++) {
        VK_TI_FRAME();
    }
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "second focus-gain re-spawns a VK");

    vk = ctx->active_virtual_keyboard;
    memcpy(vk->buffer, "DISCARD", 7);
    vk->buffer_length = 7;
    vk->submitted = false;
    vk->cancelled = true;
    vk->active = false;

    VK_TI_FRAME();

    FlString after_cancel = ui_text_input_get_text(&state);
    ASSERT_EQ_MSG(5u, (u32)after_cancel.length, "cancel leaves the input text untouched");
    ASSERT_TRUE_MSG(memcmp(after_cancel.data, "hello", 5) == 0, "cancel discards the keyboard buffer");
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard == nullptr, "cancel clears active_virtual_keyboard");
    ASSERT_TRUE_MSG(ctx->vk_target_text_input == nullptr, "cancel clears vk_target_text_input");

#undef VK_TI_FRAME
}
