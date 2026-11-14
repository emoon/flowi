#include "list_rows.h"

static u64 ids_id_at(const ListRows* rows, u32 row) {
    return rows->ids[row];
}

// A linear scan. The cursor/anchor mapping runs at most once per nav keystroke, not once per row.
static i32 ids_row_of(const ListRows* rows, u64 id) {
    for_count(i, rows->count) {
        if (rows->ids[i] == id) {
            return (i32)i;
        }
    }
    return -1;
}

static FlString ids_label_at(const ListRows* rows, u32 row) {
    return rows->labels[row];
}

static u64 items_id_at(const ListRows* rows, u32 row) {
    return rows->items[row].id;
}

static i32 items_row_of(const ListRows* rows, u64 id) {
    for_count(i, rows->count) {
        if (rows->items[i].id == id) {
            return (i32)i;
        }
    }
    return -1;
}

static FlString items_label_at(const ListRows* rows, u32 row) {
    return rows->items[row].label;
}

ListRows list_rows_from_ids(const u64* ids, const FlString* labels, u32 count) {
    ListRows rows = { .count = count, .ids = ids, .labels = labels };
    if (ids != nullptr) {
        rows.id_at = ids_id_at;
        rows.row_of = ids_row_of;
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
        .row_of = items_row_of,
        .label_at = items_label_at,
    };
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
