# One Rust crate with a private raw-binding layer

Status: accepted. Supersedes the crate split recorded in ADR-0031 and ADR-0033 (`flowi_sys`, `flowi_core`,
`flowi_core_sys`, `flowi_vfs`, `flowi_vfs_sys`).

The Rust side was seven crates: `flowi`, `flowi_macros`, and a safe/raw pair for each of the toolkit, the foundation
and the VFS. The split mirrored a time when the foundation and the VFS were separate native libraries. There is now
one `libflowi`, and every crate linked the same library; three build scripts shared one `native_link.rs`, the raw
crates re-exported each other flat, and the generated bindings named each other through crate roots.

A raw `-sys` crate earns its keep when a user needs the low-level C declarations from Rust. flowi wraps its whole
API, so nothing is left for a user to reach through it.

## Decision

`flowi` is the one library crate. `flowi_macros` stays separate because a procedural-macro crate must.

- `flowi/src/sys/` holds the raw C ABI as a private module. Its three generated trees (`foundation/`, `vfs_abi/`,
  `toolkit/`) are re-exported flat from `sys/mod.rs`, as the separate crates re-exported each other before. No
  `sys` type or function is reachable by path from outside the crate; items the public API exposes are re-exported
  from `lib.rs` explicitly.
- `flowi/src/foundation/` is the safe foundation surface (arena, jobs, logging, paths, file watcher) and
  `flowi/src/vfs/` is the safe VFS surface. Both are private modules re-exported from the crate root.
- One `build.rs` links `libflowi`. It reads `FLOWI_SKIP_NATIVE_LINK` and `FLOWI_LIB_DIR`
  (formerly `FLOWI_SYS_*`), and otherwise builds the library with cmake: shared by default, a static archive with
  the `static` feature.
- The `native` feature is gone, since no crate links a library that another already contains. `windowed`,
  `global-allocator` and `allocation-probe` remain.
- Tests that need raw declarations (panic containment, the `FlString` ABI probe, the service-wrapper fixture) are
  in-crate unit tests under `sys/`. Everything else is an integration test over the public API.

api_gen's output paths are configured per target: `rust_root` names the module the raw trees use for their flat
re-exports, and `facade_raw_path` names the module the safe façade calls the raw functions through. Both are
`crate::sys`.

## Consequences

- A host that linked the raw bindings (`flowi_sys`) directly has no replacement in the crate; it needs the C headers.
- The Cargo workspace is `flowi` and `flowi_macros`.
- `Arena`, `Jobs`, `Vfs` and the other foundation types keep their root-level paths in `flowi`.
