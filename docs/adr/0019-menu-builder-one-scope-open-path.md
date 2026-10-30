# Menu system: immediate-mode builder, one focus scope with an open path, point/element anchoring

We need a menu kit — context menus and an application menu bar — on top of the existing popup,
focus-scope, and modal infrastructure. We build **one shared core feeding both surfaces**
(context menu anchored at the cursor, menu-bar items anchored to a bar button) and made three
deliberate choices that diverge from the obvious paths:

1. **Immediate-mode builder, not a descriptor array** — even though `fl_dropdown_new` takes a flat
   `FlString*` option array. Menus carry heterogeneous, stateful (checkable/radio), and custom
   (color-swatch) content; a builder lets callers emit arbitrary widgets between items and
   expresses nesting as lexical scope (`fl_submenu_begin/end`), matching the library's
   immediate-mode model. Dropdowns deliberately stay array-based — a dropdown is a genuinely
   simpler thing and should not constrain the richer menu.

2. **The whole open menu chain lives in ONE popup focus scope with an internal open path**, not
   a stack of nested popup scopes. Click-outside ("outside the union of open rects → close
   all"), hover-to-switch-branch (collapse the deeper path, open the new one), and ESC
   (pop one level) are all single-scope path updates. Nested scopes would force generalizing the
   single `popup_focus_scope_id` into a save/restore stack and make branch-switching tear down
   and rebuild a sub-scope on every mouse move.

3. **Positioning becomes a mode on `FlPopupConfig`** — anchor-to-element or anchor-to-point —
   captured at open time (not live-following the cursor), with edge-flipping/clamping in the
   shared core. So the context menu (cursor point), the menu-bar dropdown (bar button), and
   submenus (parent item, flip to the left on overflow) all inherit off-screen avoidance. This
   is the one infrastructure change the feature forces on the popup layer, which today only
   anchors to an element.

## Considered Options

- **Descriptor-array API** (like `fl_dropdown_new`): can't host embedded custom content without a
  callback escape hatch, and forces the caller to own a parallel tree every frame — foreign to
  the immediate-mode core.
- **Nested popup scope per submenu**: harder click-outside and branch-switch semantics, and more
  churn in the popup layer.
- **Phantom 1×1 anchor element at the cursor**: works for the point case but does nothing for the
  edge-flipping math both surfaces need.

## Consequences

- Menus and dropdowns diverge in API shape on purpose.
- The menu's **open path** and per-level **highlight** are new state living *alongside* the popup
  focus scope, not reusing popup nesting (see CONTEXT.md: Open Path, Menu Highlight).
- Accelerator/hotkey **dispatch** is out of scope: items render shortcut *text* only; binding keys
  to actions while the menu is closed is a separate subsystem.
- **Type-ahead** (jump-to-item by typing) was deferred when this was written; it has since landed
  (`menu_typeahead` in `ui_menu.c`) as an additive feature that shapes none of the core state.
