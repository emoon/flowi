# The plugin vtable belongs to the host: flowi calls direct, api_gen emits per-consumer profiles

## Status

accepted (revises ADR-0030: the composed `FlUiAPI`/`g_fl_ui` vtable is no longer flowi's internal
call path nor flowi's plugin ABI). The direct-call half is the current state: the public surface is
plain exported `fl_*` functions, with no composed vtable in the headers, the sources or the
`codegen/` configuration (a single `Fl`/`fl` direct profile). The per-consumer `plugin_vtable` profile
and its TOML config described below do not exist in this repository; the ownership of the plugin
vtable was revisited by ADR-0051.

## Context

ADR-0030 made one artifact carry three jobs at once: the generated composed vtable
(`g_fl_ui->{domain}->{fn}`) was declared "the only call path … simultaneously the plugin ABI and
the hot-reload boundary." Bundling those jobs is the problem this ADR unpicks.

A vtable earns its indirection cost **only at a dynamic boundary** — a separately-compiled plugin
`.so`, or a hot-reloaded `.dll` swapped under a running host — where caller and callee are built
apart and need a frozen, indirected ABI. *Inside* flowi, `g_fl_ui->cursor->set(...)` is pure
overhead: a direct `fl_cursor_set(...)` is simpler, faster, and inlinable. The codegen win
ADR-0030 actually wanted — one IDL source killing the "spelled six times" duplication — does **not**
require a vtable; it only requires generating functions. ADR-0030 conflated "single source of
truth" (a codegen concern) with "ABI boundary" (a runtime concern) into a single struct.

The boundary that genuinely needs the indirection is the **plugin** one — and it does not belong to
flowi. The investigation that triggered this ADR established the ownership facts:

- The flat plugin vtable (a host-prefixed UI API struct with an `int version` field, ~100 flat
  function-pointer slots, host-prefixed `button(...)`-style convenience macros, and a plugin-side
  init macro/function pair) is **owned by the embedding host** (in its own `ui_api.h`). flowi only
  carried a *vendored copy* because flowi was extracted out of that host application. flowi had no
  `ui_api.h` of its own.
- That flat vtable is the **real, active** plugin API: the host's UI plugins build their
  whole UI through it. The boundary is load-bearing, just not flowi's.
- Everything *around* the boundary is also the host's: the plugin lifecycle (the host's plugin
  struct with create/destroy/update/messages/hot-reload), and the `service_api` registry through which UI is
  handed to plugins alongside ~7 other service APIs (string, vfs, jobs, metadata, log, …). UI is
  one entry in the host's registry, not a flowi export.

The boot seam links them today: the host calls
`service_api_create_base(ui_get_public_api(<UI API version>))`; the *vendored* flowi populated the
flat table by hand (a static const instance of the host's UI API struct, `{ .button =
ui_button_impl, … }`). After the prefix consolidation (ADR-0031) flowi's `ui_get_public_api`
returned the **composed** `g_fl_ui` instead, which is ABI-incompatible with the flat shape plugins
call (`->button` became a `const FlButtonAPI*`, not a callable slot). That mismatch — currently
masked by both spelling `FlUiAPI` and the pointer only round-tripping as opaque `private_data` — is
the concrete bug this ADR resolves, not merely a naming collision.

## Decision

**The plugin vtable is the host's. flowi exports a direct API. api_gen emits whatever shape each
consumer asks for, from one IDL.**

- **flowi drops the internal composed vtable.** api_gen's `direct` profile emits real `fl_*(…)`
  functions (forwarding to the existing hand-written `*_impl` bodies) plus the Rust façade. Internal
  and test callers go direct — `fl_cursor_set(...)`, not `g_fl_ui->cursor->set(...)`. The composed
  the composed UI API header and source, the `g_fl_ui` instance, `ui_get_public_api`, the vendored host `ui_api.h`,
  and flowi's `service_api` UI glue are deleted. **flowi stops knowing that plugins exist.**

- **The plugin vtable moves to the embedding host, generated there.** api_gen grows a
  `plugin_vtable` profile that emits, into the host's own tree: the flat UI API struct, the
  host-prefixed convenience macros, the version / plugin-use / init glue,
  and — crucially — a generated **adapter** (`<prefix>_ui_get_public_api()`) whose static table points
  each slot at flowi's exported `fl_*`. That adapter is exactly the ~100-slot hand-written
  population that lived in the vendored copy, now generated, owned by the
  host, with **zero host-prefix tokens inside flowi**. The host boots
  `service_api_create_base(<prefix>_ui_get_public_api())`; plugins compile unchanged.

- **One IDL, distributed; per-consumer profile config.** The `.def` IDL stays the single source of
  truth in flowi. The host reads it straight from the flowi path it already references
  (the vendored/submodule location), and runs api_gen with **its own** profile config — so the
  host's prefix naming lives in the host, the IDL lives in flowi. A profile is a committed
  **TOML/JSON file** (e.g. `flowi-profile.toml` and a host plugin profile); the `Naming` struct
  becomes runtime-constructed from it instead of the hardcoded `naming::FL`. Profile fields:
  `type_prefix`/`func_prefix`, `style` (`direct` | `plugin_vtable`), output dirs (Rust only for the
  flowi profile), and for `plugin_vtable` a **call-target naming** + include (which `fl_*` the slots
  invoke) plus the version/macro/glue knobs.

- **`plugin_vtable` is the only place a vtable is generated.** flowi's `direct` profile emits no
  vtable. If flowi itself ever needs to be hot-reloaded as a unit, that is a future, separate
  boundary and a future profile — not a reason to indirect every internal call now.

### Sequencing

flowi's side (parameterize the generator, go direct, delete the composed vtable, `ui_get_public_api`,
the vendored host header and the `service_api` UI glue) is done. Building and owning the
`plugin_vtable` output is the embedding host's step.

## Considered Options

- **(A) Distinct plugin name, vtable stays in flowi.** Rename the legacy `FlUiAPI`→`FlUiPluginAPI`
  etc. so the two coexist in flowi. Rejected: keeps a plugin ABI flowi has no business owning,
  leaves the hand-written ~100-slot population in flowi, and preserves the internal-indirection
  overhead. Solves the name clash, not the ownership or the overhead.

- **(C) Full integration — route everything through one composed vtable.** Rejected as
  the wrong direction: it doubles down on indirection as the internal call path, the exact cost this
  ADR removes. (The remaining value of that integration — collapsing duplicated wiring — is delivered by
  the `direct` profile generating it instead.)

- **flowi keeps generating a flat callable adapter as a courtesy export.** Rejected: that puts
  host-prefixed artifacts (or a flowi-named flat twin) back in flowi. The host generating its own
  adapter from flowi's IDL gives the same no-drift guarantee without flowi carrying the host's ABI.

- **The host vendors a checked-in copy of flowi's `.def`.** Rejected: reintroduces the drift surface
  the whole profile mechanism exists to remove. The host reads the IDL from the flowi path it
  already references.

- **Hand-maintain the plugin vtable in the host.** Rejected: ~100 slots kept in sync by hand is the
  duplication ADR-0030 set out to kill; generating it from the shared IDL keeps host and library in
  lockstep automatically.

## Consequences

- ADR-0030 is revised, not discarded: its single-source-IDL principle stands and strengthens (now
  serving multiple output profiles); its claim that the composed vtable is *the* internal call path
  and *the* plugin ABI is withdrawn. The vtable indirection survives only in the host's
  `plugin_vtable` output.
- The post-rebrand `FlUiAPI`/`g_fl_ui` name collision dissolves structurally: flowi keeps `FlUiAPI`
  for nothing internal-facing (it is deleted), the host owns its plugin vtable under its own
  prefix, and the vendored host `ui_api.h` leaves flowi entirely.
- flowi gets smaller and faster: no internal indirection, no `service_api`/plugin glue, no vendored
  plugin header. Its public surface is the generated direct C API + Rust façade.
- The host gains a build-time api_gen step and ownership of the plugin-boundary glue —
  appropriate, since it already owns the plugin lifecycle and the service registry. Its plugin ABI
  can now be versioned/frozen independently of flowi's internal churn.
- Plugins are unaffected: the host-prefixed `button(...)` macros and friends keep working; only where the table is built
  and by whom changes.
- api_gen becomes a general, profile-driven tool rather than a flowi-private generator, which is
  what lets a downstream host consume the shared IDL without any host-specific tokens leaking into
  flowi.
