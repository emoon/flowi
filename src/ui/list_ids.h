#pragma once

// The layout ids of everything a uniform list emits under its name. A list named N uses { N, id } with:
//
//   0                      the scroll container
//   1 and up               the rows: row + 1
//   -1, -2                 the wrapper around header and rows, and the header
//   -3, -5, -7, ...        the header cell of column index 0, 1, 2, ...
//   -4, -6, -8, ...        the resize handle of column index 0, 1, 2, ...
//   INT32_MIN and up       the Tree's expand arrows: INT32_MIN + row
//   LIST_ID_* seeds below  the marquee and the drag ghost and insertion line

#include <core/string.h>
#include <core/types.h>
#include <flowi/ui/layout.h>
#include <stdint.h>

#define LIST_ID_CONTAINER 0
#define LIST_ID_WRAPPER (-1)
#define LIST_ID_HEADER (-2)
#define LIST_ID_HEADER_CELL(i) (-3 - 2 * (i32)(i))
#define LIST_ID_HEADER_HANDLE(i) (-4 - 2 * (i32)(i))
#define LIST_ID_TREE_ARROW(row) ((i32)(INT32_MIN + (i32)(row)))

#define LIST_ID_MARQUEE_FLOAT 0x4d515251    // 'MQRQ'
#define LIST_ID_MARQUEE_FILL 0x4d515200     // 'MQR\0'
#define LIST_ID_DRAG_GHOST_FLOAT 0x44475348 // 'DGSH'
#define LIST_ID_DRAG_GHOST_FILL 0x44475300  // 'DGS\0'
#define LIST_ID_DRAG_LINE_FLOAT 0x444c4e45  // 'DLNE'
#define LIST_ID_DRAG_LINE_FILL 0x444c4e00   // 'DLN\0'

static inline FlLayoutId list_layout_id(FlString name, i32 id) {
    return (FlLayoutId) { .name = name, .id = id };
}

static inline FlLayoutId list_container_layout_id(FlString name) {
    return list_layout_id(name, LIST_ID_CONTAINER);
}

static inline FlLayoutId list_row_layout_id(FlString name, u32 row) {
    return list_layout_id(name, (i32)(row + 1));
}
