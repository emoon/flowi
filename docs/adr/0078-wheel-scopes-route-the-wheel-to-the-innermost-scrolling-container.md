# Wheel scopes route the wheel to the innermost scrolling container

Status: accepted.

Scroll areas and Lists scroll by themselves when the wheel turns over them. Scroll areas already
gave the wheel to the innermost hovered area, with logic that knew only about scroll areas. A List
has its own scroll state, so a List inside a scroll area would have scrolled together with the area.

ADR-0021 covers the other wheel path: an app that reads the wheel delta over its own element as a
Scroll Target. That path is unchanged.

## Decision

A scroll area and a List each open a wheel scope at their begin and close it at their end
(`wheel_scope.c`). A scope records its parent scope and whether it takes the wheel. The scopes are
kept across frames in one map on the shared UI state, keyed by element id.

The wheel goes to one scope: the innermost one under the pointer that takes it. A container asks
`wheel_scope_owns_wheel` and scrolls only if the answer is yes. A List takes the wheel only while
its rows do not all fit, so a short List passes it to the scroll area around it. A scroll area
always takes it.

Hover is tested against the container's clipped rectangle from the previous frame's hit test, the
same data item hover uses.

## Considered options

- **Each container consumes the wheel delta when it scrolls.** Rejected: containers run outermost
  first, so the outer one would take the wheel before the inner one is reached.
- **Make every scrolling container a Scroll Target.** Rejected: a Scroll Target has to be the
  hovered item, and over a row the hovered item is the row, not the List's container.

## Consequences

- A List in a scroll area scrolls alone, as a scroll area in a scroll area already did.
- The answer is one frame late, as hover is.
- A scroll area whose content fits still takes the wheel, so the area around it does not scroll
  while the pointer is over the inner one.
- A menu level that scrolls reads the wheel itself and opens no scope. There is also no occlusion
  check, so a menu or popup over a List scrolls both.
- A Scroll Target inside a List row does not stop the List scrolling.
- Scopes are never removed from the map. A scope whose container is no longer emitted has no item
  state, so it is never hovered and takes no wheel. The parent walk stops after 16 levels.
