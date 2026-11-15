# The Tree is a face over the List, and the app owns the expanded state

Status: accepted.

A tree view needs selection, keyboard navigation, a marquee, scrolling and virtualization. The List
already has all of them (ADR-0028). What a tree adds is an indent, an expand arrow, and Left and
Right keys that expand, collapse and move between parent and child.

## Decision

The Tree is a third face of the List, next to the declarative and the scoped face. `ui_tree.c` calls
the List's own begin, item and end through `list_private.h` and adds only what is tree-shaped:

- It draws the indent and the arrow in front of each row. The arrow is its own clickable item, so
  a press on it toggles the row and does not select it. The Tree tells the List about that press so
  the List keeps keyboard focus and starts no marquee.
- It passes one hook, `key_target_row`, that maps Left and Right to the row the cursor moves to.
- It turns drag-reorder off. Reorder and drop Into are not designed for trees yet.

The List reads its rows through a row source (`ListRows`: a count, the id of a row, the row of an
id, and a label). The id slice, the declarative items, the tree rows and a bare count each have a
constructor. A source may have no labels, which turns type-ahead off, or no ids. Apart from those
two checks the List's selection and keyboard code does not know which source it is reading.

The app owns which nodes are expanded. The Tree stores nothing about expansion. `Tree::end` reports
the row the user asked to expand or collapse, and the app changes its own data and passes the new
rows next frame. The List treats a reorder the same way: it reports the move and the app applies it.

There are two ways to pass the rows:

- `Tree::begin` takes every visible row (id, depth, has_children, expanded). Rows have stable ids,
  so a row hidden under a collapsed parent stays selected.
- `Tree::begin_virtual` takes the number of top-level rows and only the expanded nodes. The Tree
  flattens them into row ranges each frame (`tree_flat.c`), so the cost follows the number of
  expanded nodes and not the number of rows. A row has no stored id; it is identified by its row
  number (the List sees id = row + 1).

Row numbers change when a node above expands or collapses. On a frame where the row count differs
from the previous frame, the virtual Tree checks that the toggle it reported on that previous frame
explains the difference. If it does, the cursor, the anchor and the selected ids below the toggled
row move by the number of rows added or removed, and rows that disappeared are dropped. If it does
not, all three are cleared. The Tree writes the moved selection into the set the app passed.

## Considered options

- **A separate tree widget with its own selection and keyboard code.** Rejected: it would repeat
  the List's gesture state machines and the two would drift.
- **The Tree stores the expanded set.** Rejected: the app has to know the expanded nodes anyway to
  produce the rows, and flowi keeps no model state for the List either.
- **Stable ids for the virtual source.** Not done. It needs an id per row, which is the cost the
  virtual source avoids, or a callback, which the API does not use. Row numbers come first; stable
  ids are added only where a selection has to survive inserts.

## Consequences

- A fix to selection, keyboard navigation or scrolling in the List reaches the Tree.
- With the virtual source the app keeps the expanded nodes in display order and applies each toggle
  itself. `Tree::row_at` tells it which child a row is.
- The selection is one entry per row, so select-all on a node with a million children inserts a
  million ids, and each later toggle copies them. Range-based selection storage is open.
- The virtual Tree does not know whether a row has children before the row is emitted, so Right on
  a leaf reports an expand the app ignores. Type-ahead is off for the virtual source.
- An app that passes a null set to the virtual Tree gets no selection shift and has to move its own
  selection after a toggle.
- The app has to pass the new rows on the frame after the toggle. The Tree remembers a toggle for
  one frame, so rows that arrive later clear the cursor, the anchor and the selection.
- A change that leaves the row count the same is not noticed. If one node collapses and another of
  the same size expands in one frame, the selected row numbers stay and point at other nodes.
