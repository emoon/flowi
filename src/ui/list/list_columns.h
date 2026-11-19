#pragma once

#include <core/types.h>
#include <flowi/ui/list.h>

// Resolves the width of every column from the app's ColumnDefs and the width available to the row (logical px,
// the row wrapper width, so the scrollbar reserve is already out). Writes `count` entries to `out`.
//
// A column with width > 0 is fixed at max(width, min_width). The rest share what is left in proportion to their
// flex weight (0 counts as 1); a share below a column's min_width pins it at min_width and the others split the
// remainder. If nothing is left, every flexible column sits at its min_width, so a panel narrower than the
// minimums clips on the right.
void list_resolve_column_widths(const FlColumnDef* columns, u32 count, f32 available, f32* out);
