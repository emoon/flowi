# flowi ships as a shared library exporting its API; api_gen stops emitting service vtables

Status: accepted (amends ADR-0046's foundation-service-vtable carve-out)

## Context

Plugins in host applications reached flowi's foundation — heap, string,
arena, log, path, VFS — through generated per-service vtables handed over via a service
registry, with per-service integer version gates. The consuming side needed `FL_PLUGIN_USE_*`
globals, `fl_init_*_api` macros, and a dual compile mode so the same source worked linked
or vtable-routed. The embedding host decided to replace the whole plugin→host
direction with plain exported symbols and additive-only ABI discipline after release.

## Decision

flowi builds as a shared library (`libflowi.so` / `flowi.dll`) exporting its API as ordinary
C symbols through a curated, api_gen-generated export list. The host executable and every
plugin link the same library, so plugins keep `--no-undefined` link-time checking and flowi's
globals are truly single-instance across host and plugins (what the vtable handover previously
simulated by hand). One Rust crate (`flowi_sys` + `flowi`) serves both the host application
and plugins identically — the plugin-mode vtable-dispatch cargo feature is deleted, since the
`extern "C"` declarations resolve to the same exported symbols on both sides.

api_gen is retargeted: from a `.def` file it emits the plain C header, a linker
export list, and Rust `extern "C"` declarations — it no longer emits vtable structs,
per-function forwarding macros, or init/`USE` macros for services. Interfaces a *plugin
implements* (for example the VFS driver ops) remain function-pointer structs with a
single `api_version` field; only the calls-into-the-host direction loses its vtables.
Public headers must stay C++-clean, since C++ plugins now include them directly.
