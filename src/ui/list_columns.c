// Column width resolution for lists and trees with a header. Flowi resolves every width itself and emits each
// header and body cell at a fixed pixel width, so a header and its rows always line up.

#include "list_columns.h"

#define COLUMN_UNRESOLVED -1.0f // out[] marker for a flexible column that has no width yet

// One resolution in progress: the inputs, the output array and the width not yet handed out.
typedef struct ResolvePass {
    const FlColumnDef* columns;
    u32 count;
    f32* out;
    f32 remaining;
} ResolvePass;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static f32 column_weight(const FlColumnDef* col) {
    return col->flex > 0.0f ? col->flex : 1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Fixes every column with a width, marks the rest unresolved and takes the fixed widths out of remaining.
static void resolve_fixed_columns(ResolvePass* pass) {
    for_count(i, pass->count) {
        const FlColumnDef* col = &pass->columns[i];
        if (col->width > 0.0f) {
            pass->out[i] = col->width > col->min_width ? col->width : col->min_width;
            pass->remaining -= pass->out[i];
        } else {
            pass->out[i] = COLUMN_UNRESOLVED;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static f32 unresolved_weight(const ResolvePass* pass) {
    f32 total = 0.0f;
    for_count(i, pass->count) {
        if (pass->out[i] == COLUMN_UNRESOLVED) {
            total += column_weight(&pass->columns[i]);
        }
    }
    return total;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Pins the first unresolved column whose proportional share is below its minimum. Returns false when every
// share already fits.
static bool pin_one_column_below_min(ResolvePass* pass, f32 total_weight) {
    for_count(i, pass->count) {
        const FlColumnDef* col = &pass->columns[i];
        if (pass->out[i] != COLUMN_UNRESOLVED) {
            continue;
        }
        if (pass->remaining * column_weight(col) / total_weight < col->min_width) {
            pass->out[i] = col->min_width;
            pass->remaining -= col->min_width;
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Gives every unresolved column its share of what is left, or its minimum when nothing is left.
static void fill_unresolved(const ResolvePass* pass, f32 total_weight) {
    for_count(i, pass->count) {
        const FlColumnDef* col = &pass->columns[i];
        if (pass->out[i] == COLUMN_UNRESOLVED) {
            f32 share = pass->remaining * column_weight(col) / total_weight;
            pass->out[i] = pass->remaining > 0.0f ? share : col->min_width;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_resolve_column_widths(const FlColumnDef* columns, u32 count, f32 available, f32* out) {
    ResolvePass pass = { .columns = columns, .count = count, .out = out, .remaining = available };
    resolve_fixed_columns(&pass);

    f32 total_weight = unresolved_weight(&pass);
    while (total_weight > 0.0f) {
        if (pass.remaining <= 0.0f || !pin_one_column_below_min(&pass, total_weight)) {
            fill_unresolved(&pass, total_weight);
            return;
        }
        total_weight = unresolved_weight(&pass);
    }
}
