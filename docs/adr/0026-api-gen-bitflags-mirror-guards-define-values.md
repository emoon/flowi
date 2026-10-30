# api_gen bitflags-mirror guards #define values, not a mirrored enum

## Status

partly superseded — the flag type is now a real generated C enum. `FlItemFlags` is a
`typedef enum` with `FlItemFlags_*` values (`flowi/include/flowi/ui/item_flags.h`), generated from a
plain `#[attributes(bitflags)]` enum def (`api/item_flags.def`) with no `#[mirror]`; `FlKeyModifier`
follows the same shape. The Rust side is still a generated `bitflags!` struct over `u32`
(`rust/flowi_sys/src/generated/item_flags.rs`) with a size assert. The per-value `_Static_assert`
guard against `#define` macros described below no longer exists, because there are no macros left
to guard: the C header and the Rust type come from the same def. The first rejected option below
("promote the macros to a real enum") is what was eventually done. The rest is kept for rationale.

The Rust bindings need a proper flag type for `FlItemFlags` — the bitmask passed to
`fl_item_behavior(id, flags)` that opts an element into clickable / draggable /
double-click / right-click behavior. Callers should write `ItemFlags::CLICKABLE |
ItemFlags::DRAGGABLE`, not a bare `u32`, for the same reason every other crossing type is a
generated mirror rather than a raw int: type safety plus a compile-time drift guard against the
C side.

Two facts about the C side shape the decision. First, the item-flags type was a plain `uint32_t`
typedef with the values as one `#define` macro per flag (the clickable flag
is `(1 << 0)`) in the public layout header — there is **no C `enum` type** to mirror. Second, api_gen's mirror path
(`generate_mirror_enum`) only ever emits a plain `pub enum` and ignores the bitflags attribute;
the `bitflags!` code path exists solely in the unexercised, *unguarded* deferred plain-enum
generator. So there is no "mirror + bitflags" path at all, and nothing produces a drift guard
for a flag set.

The decision is to grow api_gen a **bitflags-mirror path** that drift-guards against the
existing `#define` *values*, rather than against a mirrored C enum type. A def of the form

    #[mirror(FlItemFlags, "flowi/ui/item_flags.h")]
    #[attributes(bitflags)]
    enum ItemFlags { clickable = 1, draggable = 2, double_click = 8, ... }

generates, on the Rust side, a `bitflags! { #[repr(transparent)] struct FlItemFlags: u32 { ...
} }` (pulling in the `bitflags` crate), and on the C side a guard that asserts each flag's value
against the real macro plus the underlying width:

    _Static_assert((uint32_t)<clickable macro> == 1u, "FlItemFlags.clickable value drift");
    ...
    _Static_assert(sizeof(FlItemFlags) == 4, "FlItemFlags size drift");

Crucially this leaves the C header **untouched**: `FlItemFlags` stays `uint32_t` and the per-flag
macros stay macros. The guard pins exactly what a flag binding can drift on — a renumbered or
resized macro stops the generated TU from compiling — without inventing a C type that does not
exist.

## Considered Options

- **Promote the per-flag macros to a real `typedef enum FlItemFlags` in the C header, then mirror it.** The
  "correct C" version: it would give the mirror path a genuine enum to guard by
  `sizeof`/`_Alignof` like every other mirror. Rejected for this slice. It edits a shared
  header used across the C core (blast radius beyond the bindings), opens enum-vs-`uint32_t` ABI
  and size questions for a value that crosses a plugin boundary, and **still** requires building
  api_gen's bitflags path — strictly more work and more risk than guarding the macros directly.
  Left on the table as a future C-header cleanup; if it ever happens, the def flips to a normal
  enum mirror and the value-asserts become the standard size/offset asserts.

- **Hand-write `ItemFlags` in the ergonomic `flowi` crate over the `bitflags` crate.** This is
  what we did for `UiModifiers`, but only because that enum lives in a *private* header the
  mirror system cannot reach. The item-flags type is public, so the forcing reason is absent, and a
  hand-written set has no drift guard — add a flag macro upstream and the bindings silently lag.
  Rejected: it duplicates a fact the generator can own and verify.

- **Route through the existing deferred plain-enum `bitflags!` path.** api_gen already emits
  `bitflags!` for a non-mirror `#[attributes(bitflags)]` enum. Rejected: that path is the
  explicitly-deferred, never-exercised generator (its deep clean is owed elsewhere), and it
  emits **no** C-side guard — so it gives the type without the drift protection that is the whole
  point of preferring generation over hand-writing.

## Consequences

- api_gen now has two enum-mirror shapes: the existing value-and-`sizeof` enum mirror, and the
  new bitflags mirror whose C guard is a set of per-value `_Static_assert`s plus a `sizeof`
  assert. The bitflags shape is selected by `#[attributes(bitflags)]` on a `#[mirror(...)]` enum.
- The `bitflags` crate becomes a dependency of the generated bindings crate.
- The drift guard covers value and width, not field offsets (a flag set has none). A macro that
  is *deleted* upstream surfaces as a Rust value the C no longer defines — caught at C-guard
  compile time only if the def still lists it; a macro *added* upstream is invisible until the
  def adds it (same as any mirror: the def is the source of what crosses).
- This is the convention for every future C flag-set binding (`FlKeyModifier`, signal flag
  masks, …): mirror the `#define`s as a bitflags enum guarded by value, and only promote to a
  real C enum if the C side wants one for its own reasons.
