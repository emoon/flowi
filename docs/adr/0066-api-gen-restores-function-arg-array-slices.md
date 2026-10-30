# api_gen restores function-arg array slices, ported from flowi_core

## Status

accepted. The generator now lives in its own repository (ADR-0073); the file names below are that
codebase's.

## Context

flowi's `api_gen` is a fork of flowi_core's, "forked and trimmed" (`api.pest` header). The
trimming kept the **grammar** for array arguments — `apiarg = varargs | var | array`, with
`array = "[" ~ ptr? ~ vtype ~ array_size? ~ "]"` — and the **parser** still records
`ArrayType::Unsized` / `SizedArray` in `Variable.array`. But both code generators were trimmed:
`c_gen.rs` and `rust_gen.rs` consult only the pointer qualifier (`const_pointer` / `pointer`),
never `Variable.array`. So an array arg emitted nothing usable: no length companion, and the
ergonomic façade lowered a `const_pointer` to a *single* `&T` reference, not a slice.

That is why every array-taking slot to date — `list`, `text_input`, `ui_input.list`,
`ui_list.begin` — is marked `[manual]` and passes `*void, count: u32` at the ABI with a
hand-written ergonomic wrapper in the `flowi` crate.

An audio-oriented embedding host needs arbitrary polylines (waveforms, oscilloscopes, EQ curves) callable from Rust. The
engine already has the primitive (`painter_polyline`, an N-vertex `FlPolyPoint` array), but it was
unreachable from Rust for exactly the reason above. A single straight line is just a 2-point
polyline, so this one capability covers both "draw a line" and "draw a curve."

The decision point: add yet another `[manual]` one-off, or restore the array codegen. The restore
is **not greenfield** — flowi_core's `api_gen` still implements it cleanly
(`c_gen::write_struct_variable` / the func-param path; `rust_gen::get_type`): an `Unsized` array
emits `T* name, uint32_t name_size` in C and `name: &[T]` → `as_ptr()` + `len()` in Rust.

## Decision

**Restore array-argument codegen in the forked `api_gen`, function-argument path only, ported from
flowi_core.** An `[*const T]` arg now lowers to:

- **C prototype:** `const T* name, uint32_t name_size` (mutable `[*T]` → `T*`).
- **Rust sys extern:** `name: *const T, name_size: u32`.
- **Rust façade:** `name: &[T]`, forwarding `name.as_ptr(), name.len() as u32`.

`SizedArray` and **struct-field** arrays are deliberately left out of scope (struct fields keep
their pointer-only behavior; sized arrays are unused). The shared array element type must be an
IDL-authored struct; `painter.def` gains `struct PolyPoint { x, y, intensity }`, which generates the
single `FlPolyPoint` definition that internal `render_ir.h` now includes rather than defining inline.

## Considered Options

- **Another `[manual]` slot (`*void, count`) + hand-written wrapper.** Rejected: it is the
  established pattern, but it accretes a fourth bespoke wrapper for a class of API (`polyline`,
  `bars`, and future array slots) that a one-time codegen restore covers generically. The marginal
  cost of "do it manually again" had finally exceeded the cost of restoring the trimmed feature,
  because a working reference port exists.
- **Full faithful port (struct fields + `SizedArray` too).** Rejected for now: struct-field arrays
  add layout-assertion surface and nothing needs them; `SizedArray` is unused. Scoping to the
  function-arg path keeps the change small and reviewable. The other paths can follow if a real
  consumer appears.

## Consequences

- The whole "codegen has no slice lowering" blocker is gone for function args. `bars` (the other
  array-taking painter primitive) has since been lowered the same way, and future array APIs
  declare `points: [*const T]` instead of hand-rolling `*void, count`.
- `FlPolyPoint` is now IDL-owned (single source in the generated `painter.h`); `render_ir.h`
  includes the generated header. The Rust binding and the C header agree by construction.
- A reader who sees array codegen "back" after `api.pest` says it was trimmed — and only on the
  function-arg half — finds the why here: a targeted port-back to serve the painter, not a full
  re-adoption of upstream's array surface.
</content>
</invoke>
