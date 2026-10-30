# Evict the host plugin/service subsystem by deletion; the VFS driver interface stays in flowi

## Status

accepted (executes ADR-0033 "Phase 2" / ADR-0032 step 3/4; supersedes ADR-0033's framing of that
phase as a coordinated cross-repo migration)

## Context

ADR-0033 carved the host plugin/service subsystem (`service_api`, `plugin_manager`,
`plugin_loader`, `audio_service`, `plugin_mem_load_save`, the `plugin_sdk/*` headers) into a
quarantine target `flowi/src/host/` and deferred its removal to a "Phase 2" it described as
"substantial, cross-repo" and explicitly out of scope. It also left `vfs` depending on `host`:
`flowi/src/vfs/vfs_plugin_registry.h` includes `<host/plugin_manager.h>` and
`<host/plugin_sdk/vfs_plugin.h>`, and `vfs` and `flowi_host` are mutually-referencing static
archives papered over with `$<LINK_GROUP:RESCAN,vfs,flowi_host>` — a real dependency cycle.

Scoping the actual removal showed it is **smaller than ADR-0033 feared**, because the quarantined
subsystem has almost no *live* flowi consumer:

- The UI's per-frame `plugin_manager_update()` call (`ui_update_begin`) is already
  `#ifndef VFS_STATIC_ONLY` — compiled out of flowi's own builds; `UiCtx.plugin_manager` /
  `UiCtx.service_api` are vestigial in static mode.
- `core/message_queue` (host↔plugin message bus) is consumed only by itself and
  `host/plugin_manager`.
- `audio_service` has no consumer outside `host/`.
- The only genuine entanglement is `vfs`: its own driver interface (`FlVfsPlugin`, which the app's
  `localfs` implements) is defined in `host/plugin_sdk/vfs_plugin.h`. vfs depends on the host to
  define vfs's own extension point.

A second fact shapes the seam: `FlVfsPlugin` is a single, cohesive struct — identity (`api_version`,
`plugin_name`, `plugin_info`), lifecycle (`create(const FlServiceAPI*)`, `destroy`), and the
filesystem *operations vtable* (`open`/`read`/`get_size`/`list_directory`/`clone`/`close`/
`get_capabilities`/optional ops). Its **only reference to a host type** is the `const FlServiceAPI*`
parameter of `create` — the hook a dynamically-loaded driver uses to reach host services at
construction time. flowi's statically-linked drivers are handed in already-constructed and never
call `create` (the static registry does not; see `app/localfs.c`), so that one parameter is the
sole thing tying the interface to the host.

An embedding host is still a live downstream that must be able to migrate.

## Decision

**Evict the host subsystem from flowi by deleting it outright, and keep the VFS driver interface in
flowi. The fault line: flowi owns the filesystem-driver interface; the host owns dynamic plugin
loading and the service registry.**

- **Move `FlVfsPlugin` into vfs, whole.** Keep the struct as a single cohesive interface — identity,
  lifecycle, and the operations vtable together. The struct and the vfs value types it needs
  (`FlVfsStatus`, `FlVfsDriverError`, `FlVfsDriverCapabilities`, `FlVfsOpenFlags`,
  `FlPluginCreateResult`, the result structs, `FlVfsListingCallback`, `FlVfsEntry`,
  `FlVfsMountOptions`, plus the consumer types in `vfs_api.h`/`vfs_types.h` that vfs's own functions
  return) move into vfs. They began as internal `<vfs/...>` headers (ADR-0033 bucket-3) and are now
  generated from `api/vfs/*.def` into `flowi/include/flowi/vfs/` (`vfs_plugin.h`, `vfs_types.h`).

- **`create` drops its `const FlServiceAPI*` parameter** (`create(void)`). That parameter is the
  interface's only reference to a host type, so removing it — not splitting the struct — is what
  frees the interface to live in flowi cleanly. flowi's static drivers are handed in
  already-constructed and never call `create`, so they lose nothing. `FlServiceAPI` and the
  `.so`-loading lifecycle stay host concerns and leave flowi in the follow-up slice; a host that
  loads a driver from a `.so` does its own service wiring before handing flowi the constructed
  `FlVfsPlugin` plus instance.

- **New public seam: direct registration.** `vfs_init` drops its `VfsPluginRegistry` and
  `FlServiceAPI*` parameters; drivers are handed to vfs directly (resurrecting a
  `vfs_register_driver(... const FlVfsPlugin*, void* instance)` form). The static/dynamic
  `VfsPluginRegistry` indirection — whose only purpose was hiding static-vs-dynamic behind one
  interface — is deleted; its dynamic mode was the host concern that's leaving. `flowi`'s vfs is
  **always static**: it is handed drivers, it never loads them. `VFS_STATIC_ONLY` disappears (it
  becomes the only mode).

- **Delete, don't keep behind a flag.** `flowi/src/host/`, `core/message_queue`,
  `host/audio_service`, the `flowi_host` CMake target, and the `LINK_GROUP:RESCAN` workaround are
  removed; the vestigial `UiCtx.plugin_manager`/`service_api` fields and the `plugin_manager_update`
  call go with them. After this, `vfs` depends only on `core` and the dependency DAG is acyclic with
  no link-group rescue.

- **The embedding host absorbs from history.** Because the embedding host is live, the host removal
  lands as an **isolated, cherry-pickable commit**; a migration guide documents the new
  `vfs_register_driver` seam (and `create`'s dropped `FlServiceAPI*`) and the registry that moved
  out. flowi keeps no service registry.

### Sequencing (single-repo, green at each step)

1. Move `FlVfsPlugin` + the vfs types into `flowi/src/vfs/` and drop the `const FlServiceAPI*`
   parameter from `create`; repoint vfs internals, `app/localfs`, and tests onto
   `vfs_register_driver`; drop the `service_api`/registry params from `vfs_init`; remove the
   vestigial UI plugin-manager fields and call. flowi green; `host/` now orphaned.
2. **Isolated commit:** delete `flowi/src/host/`, `core/message_queue`, the `flowi_host` target,
   `VFS_STATIC_ONLY`, and the `LINK_GROUP:RESCAN`. flowi green.
3. Docs: this ADR, the CONTEXT.md boundary term, and the host migration guide.

## Considered Options

- **Keep `host/` behind a flag, deprecate later.** Rejected: leaves dead code and the cycle's
  workaround in flowi for a downstream that can absorb from a clean isolated commit instead.
- **Extract `host/` to a sibling dir/repo as part of this work.** Rejected: drags the cross-repo
  boundary into a single-repo change; git history + the migration guide are a cleaner handoff.
- **Split `FlVfsPlugin` into a clean `FlVfsDriver` (ops-only) + a separate envelope.** Rejected: the
  struct is a cohesive whole (identity + lifecycle + ops), and its only host coupling is the `create`
  parameter. Dropping that one parameter severs the dependency without the churn of splitting the
  struct, maintaining two parallel types, and re-pointing every driver and call site at the smaller
  vtable. The split bought nothing the parameter removal does not.
- **Move `FlVfsPlugin` whole into vfs but *keep* `create(const FlServiceAPI*)`.** Rejected:
  re-imports `FlServiceAPI` (a host type) into flowi's otherwise-clean static vfs interface — the
  exact dependency the eviction exists to remove. The point of moving the struct is to leave the
  host type behind, so the parameter has to go with it.

## Consequences

- The `vfs → host` cycle and `LINK_GROUP:RESCAN` are gone; `vfs` depends only on `core`.
- `FlServiceAPI` and the dynamic plugin system no longer exist in flowi; flowi's vfs contract is the
  `FlVfsPlugin` interface (with a parameterless `create`) + `vfs_register_driver`. Dynamic `.so`
  loading is 100% a host concern.
- The ADR-0033 bucket-3 transient is resolved: `FlVfsPlugin` and the vfs types are authored in
  `api/vfs/*.def` and generated into `flowi/include/flowi/vfs/`.
- The embedding host must adopt the new seam: build its own driver registry, call each driver's
  `create` itself (wiring any host services its drivers need beforehand, since `create` no longer
  receives an `FlServiceAPI*`), and register the constructed `FlVfsPlugin`s into flowi.
- ADR-0033's "Phase 2 is substantial/cross-repo/out of scope" framing is superseded: the flowi-side
  eviction is a bounded single-repo deletion; only the embedding host's adoption is cross-repo.
