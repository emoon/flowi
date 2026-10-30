# dock_demo manual checklist

GPU path, run from the repo root. Nothing below is asserted in CI.

1. Seven panels come up over three split levels, with two tab groups: Inspector|Assets and
   Console|Log|Timeline. Each tab renders its title, never a numeric id.
2. The cursor turns to a hand over any tab, selected or not, and dragging one within its own strip reorders it.
3. Press inside a tile: that panel's body flips to ACTIVE and every other reads inactive. Moving the cursor
   across tiles without pressing changes nothing.
4. With one panel active, its bare letter increments only its own counter; the other panels' letters do nothing
   until you press into them.
5. Press 5, 6 and 7: each hidden panel's tab disappears, and when the last one goes the Console leaf hands its
   space to its neighbour. Press them again - the leaf comes back with its exact prior tab order and selection.
6. Shift+3 closes the Inspector: its tab is gone from the tree, not merely put away. Press 3 and it comes back
   seated on the floor rather than in the tab group it left.
7. Drag a tab out of its header: the layout opens a real gap where the panel will land and a translucent ghost
   carrying the panel's title rides the cursor - never two copies of the same panel, and the tiles it passes
   over stay visible through it. Escape, or releasing outside the workspace, cancels back to the seat it came
   from.
8. NO AUTOMATED DEFENCE - re-target mid-drag by sweeping the cursor from one tile to another: the gap moves
   from the old slot to the new one in a single frame, with no intermediate state to stutter through. Only the
   eye can judge this.
9. NO AUTOMATED DEFENCE - watch a tile edge while the gap moves: the 1px border stays one crisp line and never
   smears into a doubled shimmer. `dock_layout.tile_edges_land_on_whole_pixels` asserts the rounding; whether
   the raster then reads as one line is the half only the eye can judge.
10. Shrink the window until the deep splits hit the tile floor (panels clip, none vanish), then grow it back:
    the layout returns exactly as it was.
