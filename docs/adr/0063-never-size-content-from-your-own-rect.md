# Never size your content from your own element's rect

Status: accepted

## Context

An element's laid-out rect is an *output* of the layout pass, computed from the element's own
children. Clay never shrinks a container below its content's minimum, so an element whose children
want 900px reports 900px whether or not the room exists. Code that reads its own rect back and then
sizes content against that number is reading its own request, not the answer.

Inside the dock this is visible: a panel body larger than its tile still measures at the size it laid
out at. The tile clips the excess, so nothing paints over the neighbour, but a panel that sized a
virtualized list, a canvas, or a scroll page from its own rect would allocate rows for room it does
not have and claim input over the panel next door.

The same shape recurs outside the dock — a scroll viewport's content, a split pane's children, a
floating element attached to a parent — because it is a property of how layout resolves, not of
docking.

## Decision

The room an element gets is an **input from its container**, never an output of its own layout. Read
it from whatever the container publishes:

- Docked panel content: `fl_dock_panel_size(id)`.
- Scroll viewports: the container extent, not the content element's rect.

A container that hands out room publishes it as a read-back with a **`{0, 0}` = do not act on this**
answer, covering every state in which the caller has no room to size against: absent, not laid out
yet, hidden, and not currently shown.
`{0, 0}` is one answer, not a family of sentinels — a caller distinguishing "absent" from "hidden"
would be acting on the difference, which is exactly what the contract forbids.

A container that divides its room between children holds a **pixel floor** under each of them, so a deep
split cannot starve a panel down to a few pixels: in the dock, 120×80 for a tile and 80×40 for the content
below its tab header. The floor is applied where the room is read — at emit, as a clamp on the ratio the
layout is fed — and never written back, because a window resize is not a layout act: shrinking the window
and growing it back must restore the authored layout bit-identically. The splitter drag is the one
deliberate exception; it is a layout act, so it writes, and it stops at the same floor. The floor is flat
per split rather than the recursive subtree minimum — that keeps it a local, stateless read instead of a
bottom-up pass, at the cost of a starvation ceiling: a chain of N nested splits still starves its deepest
tile once the host drops below roughly N floors of room. Past that ceiling both children of a split degrade
symmetrically (half the room each), the workspace overflows and clips as above, and one error is logged per
starvation episode — a starved tile must never silently drop a leaf out of the layout.

Overflow stays a pure clip: it can never paint over or steal input from a neighbour. Because that
makes the mistake invisible, a container logs one error the first time a child overflows it, latched
per child so a per-frame mistake does not become a per-frame message.

Consequences:

- `fl_dock_panel_size` carries the `{0, 0}` contract and no PROVISIONAL banner. A background tab
  answers `{0, 0}` where it used to report its leaf's tile, and so does a starved one.
- No general content-size API ships. The rule is stated here and each container publishes its own
  read-back; a shared abstraction over one consumer would be scaffolding.
- A caller that ignores the rule still renders correctly — the clip is unconditional — but pays in a
  logged error and in work done for room it does not have.
