# Rust bindings drop the C `Fl` name prefix

The C codebase prefixes its types and functions with `Fl`/`fl_` (ADR-0003 introduced the prefix
scheme; ADR-0031 settled on the single `Fl` prefix). Those prefixes exist to avoid symbol collisions
in C's single flat namespace and to signal ownership: C has no modules, so `FlColor` and its
siblings must be spelled out in full everywhere they appear.

The Rust bindings have neither problem. Crates and modules give real namespacing, so the
collision rationale evaporates, and ownership is already expressed by which crate a type lives in
(`flowi_sys` / `flowi`). Carrying `FlColor`, `FlVec2`, `FlSignal`, `FlPolyPoint` across into Rust
just imports C's workaround as visual noise — `painter.gradient_rect(.., FlColor, FlColor,
FlColor, FlColor)` reads worse than `Color`, for no benefit.

The decision: **Rust binding types are spelled by their unprefixed IDL name; the C `Fl`
prefix is dropped on the Rust side.** `FlColor → Color`, `FlVec2 → Vec2`, `FlSignal → Signal`,
`FlElementState → ElementState`, `FlKey → Key`, `FlPolyPoint → PolyPoint`, `FlBar → Bar`, and so
on. Opaque runner handles follow the same rule (`FlApp → App`, `FlAppSettings → AppSettings`).
The C side is **unchanged**: the real C type keeps its prefix, and the api_gen drift guard
(`_Static_assert`) still pins the prefixed C type (`FlColor`). Only the Rust spelling changes.

The mechanism is the IDL. A def names the type unprefixed — `struct Color { ... }` in
`api/core/math_data.def` — and the generator derives the C spelling (`FlColor`) by adding the prefix.
`rust_gen` spells the generated Rust type and all typed args/fields by the **IDL** name (`Color`);
`c_gen` and the layout guards use the C name. The `#[repr(C)]` struct is
ABI-identical either way — the name is cosmetic to the compiler, load-bearing only for humans.

This is done **at the source**: the generated `flowi_sys` types are clean, and the ergonomic
`flowi` crate re-exports them directly (no `as` aliasing). New types (`Key`, `ItemFlags`, …)
are born unprefixed.

## Considered Options

- **Keep the C names verbatim in Rust (`FlColor`, `FlBar`).** Zero work, and a type greps to the
  same token on both sides of the FFI boundary. Rejected: it imports a C-only workaround into a
  language that does not need it, and every call site pays the ugliness ADR-0003 itself calls the
  prefixes (collision-avoidance, not aesthetics). The cross-language grep is preserved anyway by
  the generated headers, which spell the C name, and by the layout guards that still name `FlColor`.

- **Re-export under clean names only in the ergonomic crate (`pub use ... as Color`), leaving
  `flowi_sys` prefixed.** Rejected: it splits the convention — sys code and any direct-sys
  consumer still reads `FlColor`, and the `as` aliases multiply. Doing it at the generator source
  makes one rule that holds at every layer.

- **One genuine exception: the ABI string.** IDL `String` cannot map to a Rust type literally
  named `String` (it shadows the std prelude). It is an internal FFI type users never construct
  (the ergonomic API takes `impl AsRef<str>`), so it is renamed — prefix dropped, but not to the
  colliding bare name. This is the sole binding type whose Rust name is not a plain prefix-strip
  of its C name. **(Amended 2026-08-01 — the name chosen here, `FrameStr`, was wrong; see
  "Amendment" below. The exception itself stands.)**

## Consequences

- The C naming is governed by ADR-0031 (which supersedes ADR-0003). This ADR governs only the Rust
  binding layer; the two are complementary, not in tension. The C prefix unification changed the
  *C* names and the guards, but not the Rust spellings decided here.
- A type reads differently across the boundary: Rust `Color`, C `FlColor`, guard on `FlColor`.
  The def (`struct Color`) is the Rosetta stone: the IDL name is the Rust name, and the C name is
  the IDL name plus the `Fl` prefix.
- The ABI string type is the documented exception; reviewers expect every other binding type to be
  a plain prefix-strip of its C counterpart. (Amended 2026-08-01: it is spelled `RawStr`.)
- Applies to opaque handles and hand-written sys types too (`App`, `AppSettings`, the text-input
  handle), not just generated data types — one rule across the whole Rust surface. The exceptions
  are the raw opaque C handles whose plain name is taken by an ergonomic twin or that are marked as
  raw C items (`FlArena`, `FlVfsMount`, `FlUiCtx`) and the generated error companion `FlError`;
  these keep the C spelling.

## Amendment (2026-08-01): the exception is named `RawStr`, not `FrameStr`

`FrameStr` was chosen above for one reason — dodging the prelude collision with `String` — and no
thought was given to what the name asserts. It asserts a frame lifetime the type does not have.

Four different lifetimes cross the ABI as this one type, and the type distinguishes none of them:
`'static` (a packed literal), the duration of a single call (a borrowed `&str`), an arena's (the
frame arena via `fl_frame_str`, or any arena via the arena constructors), and whatever a host
return contract says. Frame-scoping is enforced, where it is enforced at all, by a runtime
`FL_ASSERT_MSG` on the C side — not by a Rust lifetime. The type is `Copy` and carries no lifetime
parameter, so a frame-arena view can be stored in a struct field and dangle after `ui_update_end`
with nothing to catch it.

A name that promises a guarantee the type does not enforce costs real time: a reader has to check
whether the compiler is doing the work the name implies, find that it is not, and remember that
for every later encounter. The rule this amendment sets is: **a type name that asserts a lifetime
or scope is reserved for a type the compiler actually binds to it.** Otherwise name it for what it
*is* — here, the raw ABI shape — and push the varying part into constructor names, where it is
true per call site rather than false everywhere.

So the type is `RawStr`, and its lifetime menu is the constructor set: `RawStr::from_static`,
`RawStr::borrow`, `RawStr::borrow_bytes`, `RawStr::in_arena`, `RawStr::in_arena_nul`, plus the
`extern "C"` `fl_frame_str` for the frame arena and a bare host return.

Two consequences worth naming:

- The helpers came with it. `fl_static`, `fl_string_borrow`, `fl_string_to_owned` and the rest were
  hand-written *Rust*, but the `fl_` prefix claims "linkable C symbol" — only `fl_frame_str` is
  one. They are now inherent members of `RawStr`, so the prefix's false claim goes away and the
  call site names the lifetime it picked.
- `RawStr` lives at the crate root (`flowi::RawStr`), not behind the `flowi::abi` module,
  because that module exists to hide raw spellings whose *root* name is taken by an ergonomic twin
  (`Icon`, `Cursor`, `TextConfig`). The safe counterpart to `RawStr` is plain `&str`, not a type,
  so there is no collision to hide from — and the name now carries its own warning.

Deliberately **not** done here: giving the type a lifetime parameter. That is the change which
would make the compiler enforce what `FrameStr` merely claimed, and it is the one that breaks
consumers storing the type in struct fields. It stays deferred; this amendment only stops the name
from lying in the meantime.
