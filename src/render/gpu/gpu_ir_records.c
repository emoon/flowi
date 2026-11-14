#include "gpu_ir_records.h"
#include <core/core.h>
#include <core/log.h>
#include <stddef.h>

// Per-kind record layout. A zeroed entry is a kind with no known payload size that commits no commands.
typedef struct RecordLayout {
    size_t payload_size; // the fixed payload struct
    size_t elem_size;    // inline trailing array element (0 = none)
    size_t count_offset; // offset of the u32 element count within the payload
    u32 cmds_fixed;      // commands committed regardless of the elements
    u32 cmds_per_elem;   // commands committed per trailing element
} RecordLayout;

#define FIXED(type) { .payload_size = sizeof(type), .cmds_fixed = 1 }
#define ARRAY(type, elem, count_field, per_elem)   \
    { .payload_size = sizeof(type),                \
      .elem_size = sizeof(elem),                   \
      .count_offset = offsetof(type, count_field), \
      .cmds_per_elem = per_elem }

static const RecordLayout k_layouts[] = {
    [FlCmd_FillRect] = FIXED(FlCmdFillRect),
    [FlCmd_Border] = FIXED(FlCmdBorder),
    [FlCmd_Image] = FIXED(FlCmdImage),
    [FlCmd_Arc] = FIXED(FlCmdArc),
    [FlCmd_Circle] = FIXED(FlCmdCircle),
    [FlCmd_Diamond] = FIXED(FlCmdDiamond),
    [FlCmd_TextRun] = ARRAY(FlCmdTextRun, FlGlyph, glyph_count, 1),
    [FlCmd_Bar] = ARRAY(FlCmdBarSet, FlBar, bar_count, 1),
    [FlCmd_Polyline] = ARRAY(FlCmdPolyline, FlPolyPoint, point_count, 2),
};

static const RecordLayout k_unknown_layout = { 0 };

static const RecordLayout* layout_of(u32 kind) {
    return kind < sizeof_array(k_layouts) ? &k_layouts[kind] : &k_unknown_layout;
}

static u32 element_count(const RecordLayout* layout, const void* payload) {
    if (layout->elem_size == 0) {
        return 0;
    }
    return *(const u32*)((const u8*)payload + layout->count_offset);
}

void gpu_ir_walk(const void* ir_commands, size_t ir_bytes, GpuRecordFn fn, void* user) {
    const u8* end = (const u8*)ir_commands + ir_bytes;
    for (const u8* p = (const u8*)ir_commands; p < end; p += ((const FlCmdHeader*)p)->byte_size) {
        const FlCmdHeader* header = (const FlCmdHeader*)p;
        if (header->byte_size == 0) {
            break; // zero-size record
        }
        fn(user, header);
    }
}

// The effect is appended right after the payload and any inline trailing array - not at the record's
// byte_size, which is rounded up to the 8-byte record stride and so can leave trailing padding past the effect.
const FlEffect* gpu_record_effect(const FlCmdHeader* header) {
    if ((header->flags & FL_HAS_EFFECT) == 0) {
        return nullptr;
    }
    const RecordLayout* layout = layout_of(header->kind);
    if (layout->payload_size == 0) {
        log_warning("gpu translate: dropping the effect on command kind %u - no payload size for it",
                    (unsigned)header->kind);
        return nullptr;
    }
    const u8* payload = (const u8*)(header + 1);
    size_t size = layout->payload_size + (size_t)element_count(layout, payload) * layout->elem_size;
    const FlEffect* e = (const FlEffect*)(payload + size);
    return e->kind != FlEffectKind_None ? e : nullptr;
}

u32 gpu_record_cmd_bound(const FlCmdHeader* header) {
    const RecordLayout* layout = layout_of(header->kind);
    return layout->cmds_fixed + layout->cmds_per_elem * element_count(layout, header + 1);
}
