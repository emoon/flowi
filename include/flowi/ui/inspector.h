#pragma once

// Render-IR inspector seam - a narrow, stable view over flowi's per-frame snapshot of its
// last render commands. fl_inspector_snapshot serializes the current UI tree into a caller-owned text
// buffer; the fl_inspector_hit_test_* pair locates an element by id or name and returns its center point
// (physical px) for click automation; the fl_inspector_element_* pair enumerates the frame's elements as
// flat records.
//
// The seam is compiled out entirely unless FLOWI_UI_INSPECTOR is defined.

#ifdef FLOWI_UI_INSPECTOR

#include <stdbool.h>
#include <stdint.h>

#include <flowi/core/string.h>

// An element located by a hit-test query: its stable id, its string name (may be empty),
// and its center point in physical px - enough to drive a click. name borrows the
// current frame's element-name storage and is valid until the next frame is built.
typedef struct FlInspectorHit {
    uint32_t id;
    FlString name;
    float x; // center x (physical px)
    float y; // center y (physical px)
} FlInspectorHit;

// An element yielded by enumeration: id, name, and full bounds (physical px). name
// has the same borrowed lifetime as FlInspectorHit::name.
typedef struct FlInspectorElement {
    uint32_t id;
    FlString name;
    float x;      // top-left x (physical px)
    float y;      // top-left y (physical px)
    float width;  // physical px
    float height; // physical px
} FlInspectorElement;

// Serialize the current frame's UI tree to buf as JSON text, snprintf-style: writes at
// most buf_size bytes including the NUL terminator, always NUL-terminating when
// `buf_size > 0`, and returns the number of bytes that WOULD be written excluding the NUL.
// A return value >= buf_size means the buffer was too small and the caller should retry
// with at least (return value + 1) bytes. The buffer is entirely caller-owned; the
// inspector keeps no reference to it. The output is always a valid JSON object; a frame with
// no inspectable elements (including before any frame has been built) yields
// `{"elements":[],"elementCount":0}`.
//
// Each element carries id, name, type, boundingBox ({x,y,width,height} in physical
// px) and zIndex, plus the fields its type has: backgroundColor + cornerRadius for a rectangle,
// borderColor + cornerRadius for a border, text + textColor + fontSize + fontId
// for text, backgroundColor for an image. Colors are {r,g,b,a} 0-255 objects and
// cornerRadius is {topLeft,topRight,bottomLeft,bottomRight}. This shape may gain fields.
int32_t fl_inspector_snapshot(char* buf, int32_t buf_size);

// Hit-test by id: fill `*out` with the element carrying element_id and return true.
// Returns false (leaving `*out` untouched) if no such element is in the current frame.
bool fl_inspector_hit_test_id(uint32_t element_id, FlInspectorHit* out);

// Hit-test by name: fill `*out` with the first element whose name equals name and
// return true, else false. Convenience over enumerate for name-driven automation.
bool fl_inspector_hit_test_name(FlString name, FlInspectorHit* out);

// Number of inspectable elements in the current frame (0 when no frame has been built).
int32_t fl_inspector_element_count(void);

// Fill `*out` with the element at index (0..count-1) and return true; false for an
// out-of-range index.
bool fl_inspector_element_get(int32_t index, FlInspectorElement* out);

#endif // FLOWI_UI_INSPECTOR
