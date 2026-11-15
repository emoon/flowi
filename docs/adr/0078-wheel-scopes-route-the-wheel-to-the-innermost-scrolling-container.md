# Wheel scopes route the wheel to the innermost scrolling container

Status: accepted.

Scroll areas and Lists scroll by themselves when the wheel turns over them. If each decides that
from its own hover test, a List inside a scroll area scrolls together with the area, and so do two
nested scroll areas.

ADR-0021 covers the other wheel path: an app that reads the wheel delta over its own element as a
Scroll Target. That path is unchanged.

## Decision

Every container that scrolls for the user opens a wheel scope at its begin and closes it at its end
(`wheel_scope.c`). A scope records its parent scope and whether it can scroll right now. The scopes
are kept across frames in one map on the shared UI state, keyed by element id.

The wheel goes to one scope: the innermost one under the pointer that can scroll. A container asks
`wheel_scope_owns_wheel` and scrolls only if the answer is yes. A List whose rows all fit registers
as not scrolling, so the wheel passes to the scroll area around it.

Hover is tested against the container's clipped rectangle from the previous frame's hit test, the
same data item hover uses.

## Considered options

- **Each container consumes the wheel delta when it scrolls.** Rejected: containers run outermost
  first, so the outer one would take the wheel before the inner one is reached.
- **Make every scrolling container a Scroll Target.** Rejected: a Scroll Target has to be the
  hovered item, and the rows of a List are the hovered items, so the List's container never is.

## Consequences

- A List in a scroll area, or a scroll area in a scroll area, scrolls one container per wheel tick.
- The answer is one frame late, as hover is.
- There is no occlusion check. A menu or popup over a List scrolls both, as stacked scroll areas
  did before.
- A Scroll Target inside a List row does not stop the List scrolling.
- Scopes are never removed from the map, and the parent walk stops after 16 levels.
