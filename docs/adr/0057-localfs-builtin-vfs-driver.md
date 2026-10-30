# LocalFS is a built-in VFS driver, registered by fl_vfs_init()

## Status

accepted

## Context

flowi's VFS is driver-based: mount/list/read/write dispatch through the `FlVfsPlugin` vtable, and
`fl_vfs_init()` starts with no drivers registered. Historically flowi shipped *no* driver at all — the
core was pure mechanism, and every consumer had to hand in a driver before it could resolve a single
path. The Rust-bindings runner did this with a POSIX-only static driver that lived off in `app/`
(`app/localfs.c`), and a downstream host re-implemented the same idea again as a loadable, cross-platform DLL
plugin. Two independent LocalFS drivers, zero shared code, and a VFS that "cannot read a file" until
a host wires one up.

That last property is the problem: a filesystem toolkit that cannot open a local file out of the box
makes no sense. Reading native files is not an app concern — it is the floor the VFS stands on.

Two facts make a built-in driver clean rather than a layering violation:

- **Built-in is not dynamically loaded.** flowi never `dlopen`s driver code (ADR-0034); drivers are
  handed in already constructed. A driver that flowi *links* and *registers itself* keeps that
  invariant intact — nothing is loaded, it is just always present.
- **`os.h` already abstracts the platform.** `file_open_os` / `file_read_from_handle_os` /
  `file_stat_os` / `dir_open_os` and friends cover every filesystem operation the driver needs, so a
  single cross-platform driver has no `#ifdef` of its own — the platform forks stay in `os/posix.c`
  and `os/windows.c` where they belong.

## Decision

Ship **LocalFS as a built-in driver in the vfs library** (`flowi/src/vfs/drivers/localfs.c`), registered by
`fl_vfs_init()`. The VFS reads the native filesystem out of the box; hosts add archive/network drivers
via `vfs_register_driver()` as before.

- The driver is stateless and self-describing (each handle carries its absolute OS path plus, for
  files, an OS fd). All I/O goes through `os.h` — no platform code in the driver.
- LocalFS is the **catch-all fallback** (`can_open` accepts any existing path), so it must stay last
  in the driver list. `VfsState::builtin_fallback` pins it at the tail: `vfs_register_driver()`
  splices host drivers in *ahead* of it, preserving "specific drivers (archives, network) tried
  before LocalFS."
- `app/localfs.c` and its manual `vfs_register_driver(localfs_plugin(), …)` in the runner are
  deleted; the runner relies on the built-in.

## Consequences

- Every flowi consumer — runner, tests, and downstream hosts — gets native file access
  from `fl_vfs_init()` with no wiring. A host's separate LocalFS plugin becomes redundant and can be
  removed, deleting a duplicated, platform-forked implementation.
- The driver-ordering contract is now enforced structurally (tail-pinned fallback) instead of relying
  on hosts registering LocalFS last by convention.
- Pre-existing behavior is preserved, including the lack of mount-root confinement (absolute paths and
  `..` are used verbatim). Hardening that is out of scope here.
