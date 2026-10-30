// Render-IR inspector. Reads the internal UiRenderCommand IR through the narrow
// accessors (ui_get_render_command_count / ui_get_render_command in ui_private.h) and re-serializes
// it onto flowi's side of the boundary. Compiled out entirely unless FLOWI_UI_INSPECTOR is defined - see
// include/flowi/ui/inspector.h.

#include <flowi/ui/inspector.h>

#ifdef FLOWI_UI_INSPECTOR

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui_private.h"

// Scissor/None commands are layout plumbing, not user-facing elements.
static bool inspector_is_element(UiRenderCommandType type) {
    switch (type) {
        case UiRenderCmd_Rectangle:
        case UiRenderCmd_Border:
        case UiRenderCmd_Text:
        case UiRenderCmd_Image:
        case UiRenderCmd_Custom:
            return true;
        default:
            return false;
    }
}

static const char* inspector_type_name(UiRenderCommandType type) {
    switch (type) {
        case UiRenderCmd_Rectangle:
            return "rectangle";
        case UiRenderCmd_Border:
            return "border";
        case UiRenderCmd_Text:
            return "text";
        case UiRenderCmd_Image:
            return "image";
        case UiRenderCmd_ScissorStart:
            return "scissor_start";
        case UiRenderCmd_ScissorEnd:
            return "scissor_end";
        case UiRenderCmd_Custom:
            return "custom";
        default:
            return "none";
    }
}

// snprintf-style append into a caller buffer: writes into buf + off when it fits, otherwise
// only counts. Returns the running logical length (which may exceed buf_size - the standard
// truncation-detection contract). vsnprintf NUL-terminates within the size it is given.
static int32_t inspector_appendf(char* buf, int32_t buf_size, int32_t off, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written;
    if (buf && off < buf_size) {
        written = vsnprintf(buf + off, (size_t)(buf_size - off), fmt, ap);
    } else {
        written = vsnprintf(NULL, 0, fmt, ap);
    }
    va_end(ap);
    if (written < 0) {
        written = 0;
    }
    return off + written;
}

// Append s as a JSON string literal (with surrounding quotes), escaping the characters JSON requires.
static int32_t inspector_append_json_string(char* buf, int32_t buf_size, int32_t off, FlString s) {
    off = inspector_appendf(buf, buf_size, off, "\"");
    for (u64 i = 0; i < s.length; ++i) {
        unsigned char c = (unsigned char)s.data[i];
        switch (c) {
            case '"':
                off = inspector_appendf(buf, buf_size, off, "\\\"");
                break;
            case '\\':
                off = inspector_appendf(buf, buf_size, off, "\\\\");
                break;
            case '\n':
                off = inspector_appendf(buf, buf_size, off, "\\n");
                break;
            case '\r':
                off = inspector_appendf(buf, buf_size, off, "\\r");
                break;
            case '\t':
                off = inspector_appendf(buf, buf_size, off, "\\t");
                break;
            default:
                if (c < 0x20) {
                    off = inspector_appendf(buf, buf_size, off, "\\u%04x", (unsigned)c);
                } else {
                    off = inspector_appendf(buf, buf_size, off, "%c", (char)c);
                }
                break;
        }
    }
    return inspector_appendf(buf, buf_size, off, "\"");
}

// Append `"<key>":{"r":..,"g":..,"b":..,"a":..}` for one of the command's colors.
static int32_t inspector_append_color(char* buf, int32_t buf_size, int32_t off, const char* key, UiColor4 c) {
    return inspector_appendf(buf, buf_size, off, ",\"%s\":{\"r\":%u,\"g\":%u,\"b\":%u,\"a\":%u}", key, (unsigned)c.r,
                             (unsigned)c.g, (unsigned)c.b, (unsigned)c.a);
}

// Append "cornerRadius":{...} with the four per-corner radii.
static int32_t inspector_append_corner_radius(char* buf, int32_t buf_size, int32_t off, FlCornerRadius cr) {
    return inspector_appendf(
        buf, buf_size, off,
        ",\"cornerRadius\":{\"topLeft\":%.2f,\"topRight\":%.2f,\"bottomLeft\":%.2f,\"bottomRight\":%.2f}",
        (double)cr.tl, (double)cr.tr, (double)cr.bl, (double)cr.br);
}

// Append the fields that only exist for a given command type.
static int32_t inspector_append_type_fields(char* buf, int32_t buf_size, int32_t off, const UiRenderCommand* cmd) {
    switch (cmd->type) {
        case UiRenderCmd_Rectangle:
            off = inspector_append_color(buf, buf_size, off, "backgroundColor", cmd->bg_color);
            off = inspector_append_corner_radius(buf, buf_size, off, cmd->corner_radius);
            break;
        case UiRenderCmd_Border:
            off = inspector_append_color(buf, buf_size, off, "borderColor", cmd->border_color);
            off = inspector_append_corner_radius(buf, buf_size, off, cmd->corner_radius);
            break;
        case UiRenderCmd_Text:
            if (cmd->text.length > 0) {
                off = inspector_appendf(buf, buf_size, off, ",\"text\":");
                off = inspector_append_json_string(buf, buf_size, off, cmd->text);
            }
            off = inspector_append_color(buf, buf_size, off, "textColor", cmd->text_color);
            off = inspector_appendf(buf, buf_size, off, ",\"fontSize\":%d,\"fontId\":%d", (int)cmd->font_size,
                                    (int)cmd->font_id);
            break;
        case UiRenderCmd_Image:
            off = inspector_append_color(buf, buf_size, off, "backgroundColor", cmd->bg_color);
            break;
        default:
            break;
    }
    return off;
}

int32_t fl_inspector_snapshot(char* buf, int32_t buf_size) {
    if (buf && buf_size > 0) {
        buf[0] = '\0';
    }

    const int32_t count = ui_get_render_command_count();
    int32_t off = inspector_appendf(buf, buf_size, 0, "{\"elements\":[");

    int32_t emitted = 0;
    for (int32_t i = 0; i < count; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || !inspector_is_element(cmd.type)) {
            continue;
        }

        off = inspector_appendf(buf, buf_size, off, emitted == 0 ? "{" : ",{");
        ++emitted;

        off = inspector_appendf(buf, buf_size, off, "\"id\":%u,\"name\":", cmd.id);
        off = inspector_append_json_string(buf, buf_size, off, cmd.name);
        off = inspector_appendf(buf, buf_size, off, ",\"type\":\"%s\"", inspector_type_name(cmd.type));
        off = inspector_appendf(buf, buf_size, off,
                                ",\"boundingBox\":{\"x\":%.2f,\"y\":%.2f,\"width\":%.2f,\"height\":%.2f}",
                                (double)cmd.x, (double)cmd.y, (double)cmd.width, (double)cmd.height);
        off = inspector_appendf(buf, buf_size, off, ",\"zIndex\":%d", (int)cmd.z_index);
        off = inspector_append_type_fields(buf, buf_size, off, &cmd);
        off = inspector_appendf(buf, buf_size, off, "}");
    }

    return inspector_appendf(buf, buf_size, off, "],\"elementCount\":%d}", emitted);
}

int32_t fl_inspector_element_count(void) {
    const int32_t count = ui_get_render_command_count();
    int32_t n = 0;
    for (int32_t i = 0; i < count; ++i) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && inspector_is_element(cmd.type)) {
            ++n;
        }
    }
    return n;
}

bool fl_inspector_element_get(int32_t index, FlInspectorElement* out) {
    if (index < 0 || !out) {
        return false;
    }

    const int32_t count = ui_get_render_command_count();
    int32_t n = 0;
    for (int32_t i = 0; i < count; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || !inspector_is_element(cmd.type)) {
            continue;
        }
        if (n == index) {
            out->id = cmd.id;
            out->name = cmd.name;
            out->x = cmd.x;
            out->y = cmd.y;
            out->width = cmd.width;
            out->height = cmd.height;
            return true;
        }
        ++n;
    }
    return false;
}

// Fill a hit result from a command's element rect: the center point is what click-automation drives.
static void inspector_fill_hit(FlInspectorHit* out, const UiRenderCommand* cmd) {
    out->id = cmd->id;
    out->name = cmd->name;
    out->x = cmd->x + cmd->width * 0.5f;
    out->y = cmd->y + cmd->height * 0.5f;
}

bool fl_inspector_hit_test_id(uint32_t element_id, FlInspectorHit* out) {
    if (!out) {
        return false;
    }

    const int32_t count = ui_get_render_command_count();
    for (int32_t i = 0; i < count; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || !inspector_is_element(cmd.type)) {
            continue;
        }
        if (cmd.id == element_id) {
            inspector_fill_hit(out, &cmd);
            return true;
        }
    }
    return false;
}

bool fl_inspector_hit_test_name(FlString name, FlInspectorHit* out) {
    if (!out || name.data == NULL || name.length == 0) {
        return false;
    }

    const int32_t count = ui_get_render_command_count();
    for (int32_t i = 0; i < count; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || !inspector_is_element(cmd.type)) {
            continue;
        }
        if (cmd.name.data != NULL && cmd.name.length == name.length
            && memcmp(cmd.name.data, name.data, (size_t)name.length) == 0) {
            inspector_fill_hit(out, &cmd);
            return true;
        }
    }
    return false;
}

#endif // FLOWI_UI_INSPECTOR
