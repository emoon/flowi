# Clay is vendored with documented local patches

## Status

accepted. Supersedes ADR-0012.

## Context

ADR-0012 proposed forking Clay into a flowi-owned engine: a mechanical `Clay_* -> Fl*` rename of the layout structs,
with `external/clay/` deleted and the layout engine moved under `ui/`. That fork was never carried out. The code
still vendors Clay under `external/clay/` (`clay.h`, `clay.c`) with its `Clay_*` symbols, and `flowi/src/ui/layout.c`
still translates the public layout config and sizing structs into `Clay_ElementDeclaration`. The ADRs and code do not
record why the rename was not pursued, only that the vendored copy took targeted algorithm edits instead of a
rename-and-own fork.

ADR-0012's premise of a pristine upstream drop therefore does not hold: the vendored `clay.h` carries local changes.
Its header states that it is an altered version of Clay by Nic Barker, modified for flowi, and that altered regions
are marked with `FLOWI:` or `flowi divergence` comments, under the zlib license reproduced at the end of the file.

## Decision

Keep Clay vendored under `external/clay/` rather than forking it into an owned engine.

- Local patches stay small and targeted, each marked in `clay.h` with a `FLOWI:` or `flowi divergence` comment so
  a diff against upstream can be found by searching for those markers.
- Every patch is recorded in the list below. A new patch adds an entry here.
- Nic Barker's copyright notice and the altered-version note stay in `clay.h`, as the license requires.

### Recorded local patches

Line numbers refer to `external/clay/clay.h` at the time of writing.

1. **Placed children (ADR-0036).** An element can opt into explicit content-space placement: `Clay_FlowiPlacedConfig`
   (about line 571) with `enabled` and an `offset`, the `Clay__ElementIsPlaced` helper (1295, guarded on
   `!isTextElement` because `config` is a union), exclusion of placed children from the parent's size, minimum
   size and child-gap count (1980) and from the flex share (2472), exclusion from the content extent (3295), and
   positioning at the explicit offset instead of the flex cursor, with no advance of that cursor (3363, 3383).
2. **Paint z-order and image tint (ADR-0009).** The element's own `color:` background is emitted before its
   custom-paint span, and the span is bracketed in a scissor of the element's own rect (3206), so the per-element
   order is background, own scissor, paint span, children, border. An image element's `backgroundColor` is the image
   tint and no longer also emits a fill rectangle (3214).
3. **Text overflow and elide (ADR-0065).** `Clay_TextOverflow` with `CLIP` and `ELIDE` (403) and the matching field
   on the text config (438); an eliding run has no hard minimum width (2213) and is resizable (2485); the elide
   truncation to the longest whole-codepoint prefix plus an ellipsis (U+2026), synthesised into per-frame scratch
   (2720).
4. **Measure cache does not cache a not-ready font (ADR-0024).** A measurement whose height is zero for a non-empty
   string is released and returned as a default instead of being linked into the cache (1847), so a font still
   loading does not poison the entry.
5. **Text vertical alignment.** `Clay_TextVerticalAlignment` (391), the `verticalAlignment` field on the text config
   (436) and on `Clay_TextRenderData` (712), and the render-pass wiring (3163).
6. **Open-element custom data slot.** `Clay_GetOpenElementCustomDataSlot` (declared 1011, defined 1481) returns the
   address of the open element's custom `customData` field.

## Considered alternatives

- **Fork into an owned `Fl*` engine (ADR-0012).** Not pursued; superseded by this ADR. Vendoring with markers
  gets the algorithm edits flowi needed without deleting `external/clay/` and re-homing the engine.
- **Keep Clay pristine.** Not possible once the patches above were needed; the translation layer in `layout.c` alone
  cannot express placed children, paint ordering or eliding.

## Consequences

- An upstream upgrade is a replace-and-reapply, not a text merge: take the new `clay.h` verbatim and re-apply each
  patch above, locating them via the `FLOWI:` / `flowi divergence` markers.
- The vendored copy is compiled with `-w`, so upstream warnings are not part of flowi's warning regime.
- Public layout structs in `layout.c` remain ABI mirrors of Clay's, pinned by `_Static_assert`s, until a separate
  decision changes that.
- `clay_to_render_ir.c` keeps the output-side conversion from `Clay_Color` (float) to `FlColor`; its helper is
  still named `to_em_color`.
