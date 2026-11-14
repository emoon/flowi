#include "ui.h"
#include <core/log.h>
#include "font_cache.h"
#include "ui_private.h"
#include <flowi/ui/font.h>

void fl_font_style_register(FlFontId font_id, FlFontStyle style) {
    FL_VALIDATE(g_ui_ctx != nullptr);

    if (style < 0 || style >= FontStyle_Count) {
        log_error("Trying to register a style outside of bounds! skipping");
        return;
    }

    g_ui_ctx->font_styles[style] = font_id;
}

void fl_font_style_set(FlFontStyle style) {
    FL_VALIDATE(g_ui_ctx != nullptr);

    if (style < 0 || style >= FontStyle_Count) {
        log_error("Trying to register a style outside of bounds! skipping");
        return;
    }

    g_ui_ctx->active_font = g_ui_ctx->font_styles[style];
}

void ui_font_set(FlFontId id) {
    FL_VALIDATE(g_ui_ctx != nullptr);
    g_ui_ctx->active_font = id;
}

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

// The public face of ui_font_load, for a host that loads fonts from its own data mount instead of the asset
// mount fl_font_load resolves against.
FlFontId fl_font_load_mount(FlVfsMount* mount, FlString relative_path, u16 default_size) {
    return ui_font_load(mount, relative_path, default_size);
}

bool fl_font_load_fallback(FlVfsMount* mount, FlString relative_path) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, false);
    return font_cache_load_fallback(g_ui_ctx->shared->font_cache, mount, relative_path);
}

void ui_font_wait_load(FlFontId font_id) {
    FL_VALIDATE(g_ui_ctx != nullptr);
    font_cache_wait_loaded(g_ui_ctx->shared->font_cache, font_id);
}

bool ui_font_is_loaded(FlFontId id) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, false);
    return font_cache_get_font_status(g_ui_ctx->shared->font_cache, id) == FontStatus_Ready;
}

// Pump I/O until every kicked-off font has its metrics ready (or failed), so setup can load several fonts and
// sync once before the first rendered frame. Returns false if any font failed to load.

bool ui_font_ensure_loaded(void) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, false);
    return font_cache_ensure_all_loaded(g_ui_ctx->shared->font_cache);
}

FlFontId fl_font_load_default(u16 default_size) {
    FL_VALIDATE_RET(g_ui_ctx != nullptr, FONT_ID_INVALID);
    return font_cache_load_default_font(g_ui_ctx->shared->font_cache, authored_font_size(default_size));
}
