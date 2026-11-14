#pragma once

#include <core/string.h>
#include <core/types.h>
#include <flowi/ui/list.h>

typedef struct ListRows ListRows;

struct ListRows {
    u32 count;
    const u64* ids;
    const FlString* labels;
    const FlListItem* items;
    u64 (*id_at)(const ListRows* rows, u32 row);
    i32 (*row_of)(const ListRows* rows, u64 id);
    FlString (*label_at)(const ListRows* rows, u32 row);
};

ListRows list_rows_from_ids(const u64* ids, const FlString* labels, u32 count);

ListRows list_rows_from_items(const FlListItem* items, u32 count);

bool list_rows_have_ids(const ListRows* rows);

i32 list_rows_row_of(const ListRows* rows, u64 id);
