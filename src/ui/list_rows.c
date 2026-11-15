#include "list_rows.h"

// A linear scan. The cursor/anchor mapping runs at most once per nav keystroke, not once per row.
static i32 scan_row_of(const ListRows* rows, u64 id) {
    for_count(i, rows->count) {
        if (rows->id_at(rows, i) == id) {
            return (i32)i;
        }
    }
    return -1;
}

static u64 ids_id_at(const ListRows* rows, u32 row) {
    return rows->ids[row];
}

static FlString ids_label_at(const ListRows* rows, u32 row) {
    return rows->labels[row];
}

static u64 items_id_at(const ListRows* rows, u32 row) {
    return rows->items[row].id;
}

static FlString items_label_at(const ListRows* rows, u32 row) {
    return rows->items[row].label;
}

static u64 tree_id_at(const ListRows* rows, u32 row) {
    return rows->tree_rows[row].id;
}

ListRows list_rows_from_ids(const u64* ids, const FlString* labels, u32 count) {
    ListRows rows = { .count = count, .ids = ids, .labels = labels };
    if (ids != nullptr) {
        rows.id_at = ids_id_at;
        rows.row_of = scan_row_of;
    }
    if (labels != nullptr) {
        rows.label_at = ids_label_at;
    }
    return rows;
}

ListRows list_rows_from_items(const FlListItem* items, u32 count) {
    if (items == nullptr) {
        return (ListRows) { 0 };
    }
    return (ListRows) {
        .count = count,
        .items = items,
        .id_at = items_id_at,
        .row_of = scan_row_of,
        .label_at = items_label_at,
    };
}

ListRows list_rows_from_tree(const FlTreeRow* tree_rows, const FlString* labels, u32 count) {
    if (tree_rows == nullptr) {
        return (ListRows) { 0 };
    }
    ListRows rows = { .count = count, .labels = labels, .tree_rows = tree_rows };
    rows.id_at = tree_id_at;
    rows.row_of = scan_row_of;
    if (labels != nullptr) {
        rows.label_at = ids_label_at;
    }
    return rows;
}

bool list_rows_have_ids(const ListRows* rows) {
    return rows->id_at != nullptr && rows->count > 0;
}

i32 list_rows_row_of(const ListRows* rows, u64 id) {
    if (rows->row_of == nullptr) {
        return -1;
    }
    return rows->row_of(rows, id);
}
