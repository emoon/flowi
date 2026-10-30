#ifdef FLOWI_UI_INSPECTOR

#include <core/arena.h>
#include <core/string.h>
#include "external/clay/clay.h"
#include "ui_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Forward-declare a Clay internal function and its default sentinel (defined in clay.c via
// CLAY_IMPLEMENTATION). This mirrors Clay_LayoutElementHashMapItem so the first two fields (boundingBox,
// elementId) sit at matching offsets; only elementId.stringId is read.
typedef struct {
    Clay_BoundingBox boundingBox;
    Clay_ElementId elementId;
    void* layoutElement;
    void (*onHoverFunction)(Clay_ElementId elementId, Clay_PointerData pointerInfo, void* userData);
    void* hoverFunctionUserData;
    int32_t nextIndex;
    uint32_t generation;
    bool appearedThisFrame;
    struct {
        bool collision;
        bool collapsed;
    } debugData;
} Clay_LayoutElementHashMapItem_Fwd;

extern Clay_LayoutElementHashMapItem_Fwd Clay_LayoutElementHashMapItem_DEFAULT;
Clay_LayoutElementHashMapItem_Fwd* Clay__GetHashMapItem(uint32_t id);

FlString ui_get_clay_element_name(u32 element_id) {
    Clay_LayoutElementHashMapItem_Fwd* item = Clay__GetHashMapItem(element_id);
    if (!item || item == &Clay_LayoutElementHashMapItem_DEFAULT) {
        return string_empty();
    }
    Clay_String sid = item->elementId.stringId;
    return (FlString) { .data = sid.chars, .length = (u64)sid.length };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i32 ui_get_render_command_count(void) {
    return g_ui_ctx ? g_ui_ctx->last_render_command_count : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static UiRenderCommandType map_clay_command_type(Clay_RenderCommandType t) {
    switch (t) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
            return UiRenderCmd_Rectangle;
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
            return UiRenderCmd_Border;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
            return UiRenderCmd_Text;
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            return UiRenderCmd_Image;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            return UiRenderCmd_ScissorStart;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            return UiRenderCmd_ScissorEnd;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            return UiRenderCmd_Custom;
        default:
            return UiRenderCmd_None;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Convert one Clay command into the Clay-independent snapshot type, copying the strings it exposes into
// arena (the capture fields on FlUiCtx own them; see their declaration).
static void capture_render_command(UiRenderCommand* out, const Clay_RenderCommand* cmd, FlArena* arena) {
    out->id = cmd->id;
    out->z_index = cmd->zIndex;
    out->type = map_clay_command_type(cmd->commandType);
    out->x = cmd->boundingBox.x;
    out->y = cmd->boundingBox.y;
    out->width = cmd->boundingBox.width;
    out->height = cmd->boundingBox.height;
    out->name = fl_string_copy(arena, ui_get_clay_element_name(cmd->id));

    switch (cmd->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const Clay_RectangleRenderData* r = &cmd->renderData.rectangle;
            out->bg_color
                = (UiColor4) { r->backgroundColor.r, r->backgroundColor.g, r->backgroundColor.b, r->backgroundColor.a };
            out->corner_radius = (FlCornerRadius) { r->cornerRadius.topLeft, r->cornerRadius.topRight,
                                                    r->cornerRadius.bottomLeft, r->cornerRadius.bottomRight };
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            const Clay_TextRenderData* t = &cmd->renderData.text;
            if (t->stringContents.chars && t->stringContents.length > 0) {
                const FlString text = { .data = t->stringContents.chars, .length = (u64)t->stringContents.length };
                out->text = fl_string_copy(arena, text);
            }
            out->text_color = (UiColor4) { t->textColor.r, t->textColor.g, t->textColor.b, t->textColor.a };
            out->font_id = t->fontId;
            out->font_size = t->fontSize;
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
            const Clay_ImageRenderData* img = &cmd->renderData.image;
            out->bg_color = (UiColor4) { img->backgroundColor.r, img->backgroundColor.g, img->backgroundColor.b,
                                         img->backgroundColor.a };
            out->corner_radius = (FlCornerRadius) { img->cornerRadius.topLeft, img->cornerRadius.topRight,
                                                    img->cornerRadius.bottomLeft, img->cornerRadius.bottomRight };
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            const Clay_BorderRenderData* b = &cmd->renderData.border;
            out->border_color = (UiColor4) { b->color.r, b->color.g, b->color.b, b->color.a };
            out->corner_radius = (FlCornerRadius) { b->cornerRadius.topLeft, b->cornerRadius.topRight,
                                                    b->cornerRadius.bottomLeft, b->cornerRadius.bottomRight };
            break;
        }
        default:
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Snapshot the frame's Clay output into the context's owned, Clay-independent capture. Called from
// ui_update_end, the one point where Clay's arrays and the caller strings they reference are all live.
void ui_capture_render_commands(FlUiCtx* self, Clay_RenderCommandArray* commands) {
    fl_arena_rewind(self->render_command_arena);
    self->last_render_commands = nullptr;
    self->last_render_command_count = 0;

    const i32 count = commands->length;
    if (count <= 0) {
        return;
    }

    UiRenderCommand* captured = arena_alloc_array_zero(self->render_command_arena, UiRenderCommand, count);
    for (i32 i = 0; i < count; ++i) {
        const Clay_RenderCommand* cmd = Clay_RenderCommandArray_Get(commands, i);
        if (cmd) {
            capture_render_command(&captured[i], cmd, self->render_command_arena);
        }
    }

    self->last_render_commands = captured;
    self->last_render_command_count = count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_get_render_command(i32 index, UiRenderCommand* out) {
    if (!g_ui_ctx || index < 0 || index >= g_ui_ctx->last_render_command_count || !out) {
        return false;
    }

    *out = g_ui_ctx->last_render_commands[index];
    return true;
}

#endif // FLOWI_UI_INSPECTOR
