# api_gen generates the public extension interfaces, not only the call API

## Status

accepted (revises ADR-0030, which scoped the IDL to the `fl_*` call API; extends it to the public
extension interfaces a user *implements*)

Amended: the render backend (`RenderBackend`, `RenderFrame`, the render IR and the glyph/vector read
payloads) is internal to flowi and no longer a public extension interface. It is hand-written under
`flowi/src/ui/` and has no `.def`. Only `PixelReadback`, used by `fl_app_read_pixels` and
`fl_host_read_pixels`, stays generated and public. The render-backend passages below describe the earlier
state.

## Context

ADR-0030 made the `.def` IDL the single source for flowi's public **call** API — the `fl_*`
functions a user *invokes* — generating both the C headers and the Rust `#[repr(C)]` mirror with a
`static_assert`/layout-guard drift gate. It deliberately left everything else hand-written, and in
practice that "everything else" quietly grew to include a second category of *public* surface: the
fn-pointer **extension interfaces** a user *implements* and injects — `FlRenderBackend` (ADR-0001),
`FlVfsPlugin` (ADR-0034), and the platform `display`/`input`/`audio` backends — plus a few POD call
APIs (`app`/`input`/`memory_tracker`), the ergonomic Rust façade slots, and the hand-packed bitfield
ABI structs.

These extension vtables are as much a published ABI contract as the call API: a host links against
their exact byte layout and fn-pointer signatures. Yet they were hand-maintained in C with a
hand-written Rust mirror, so every signature change had to be mirrored by hand in two languages with
no drift guard — the exact failure mode ADR-0030 introduced the IDL to kill, left unaddressed for
half the public surface.

Three facts make generating them tractable:

- **The grammar already parses struct function-fields.** `api_parser.rs` collects `Rule::function`
  entries inside a struct into `Struct.functions` (`fill_field_list`); the C emitter and `layout.rs`
  simply *drop* them today. Emitting them is a new emitter path, not a new parser.
- **The render IR is already opaque at the seam.** `render_backend.h` types
  `FlRenderFrame.commands` as `const void*` (an arena of `FlCmdHeader`-prefixed records), not a typed
  command-stream pointer. So a generated `FlRenderFrame` is both byte-identical *and* type-identical
  to the hand-written one — the IR never has to enter the IDL for the vtable to generate cleanly.
- **The transitive type closure is shallow and bimodal.** The vtables reference exactly two kinds of
  non-POD type: pointer-only deps the generated header never dereferences (`FontCache`, `UiCtx`,
  resource handles), and read-payload structs defined in hand-written *public* headers (the render
  IR, `ImageDecodeResult`, glyph/vector outlines). Neither needs to be authored into the IDL.

## Decision

**api_gen owns the public *surface* — both the public **call** API (`fl_*` a user invokes) and the
public **extension** interfaces (vtables a user implements). Internal-only types stay
hand-written; the public-extension-ABI subset becomes generated public headers, guarded by the same
layout gate as the call API.** Two new IDL constructs and a three-bucket type line make this
possible without dragging the render IR or every transitive type into the IDL.

### Two new IDL constructs

**1. Callback codegen — two shapes, one ABI rule.**

- *Struct function-fields* (the vtable case). A `ret name(args)` entry inside a struct emits as a C
  member `ret (*name)(args);`, sized `{8,8}` in `layout.rs`. This is how `FlRenderBackend`,
  `FlVfsPlugin`, and the platform backends generate.
- *Named `callback Foo(...)` typedef* (the fn-pointer-as-argument case). Reintroduces the deleted
  `callback` construct: C emits `typedef ret (*FlFoo)(args);`; the type is usable as a function
  argument or a struct-field type. This is how `AppFrameFn` and `FlVfsListingCallback` generate.
- *Rust mirror.* A fn-pointer slot mirrors as `Option<extern "C" fn(args) -> ret>`. The `Option`
  wrapper is **mandatory for nullable slots** (optional vtable ops, nullable callback args) and is
  ABI-identical to a bare `extern "C" fn` for the non-null case — `Option<extern "C" fn>` is the
  null-pointer-optimized niche, so it is the safe default for every generated slot.

**2. The `extern opaque` type construct + the `mod` directive.** A type the IDL references but does
not define is reached one of two ways:

- *`extern opaque <Name>`.* Emits a forward declaration (`struct FlFoo;` / `typedef struct FlFoo
  FlFoo;`), no `#include`, no body. For pointer-only dependencies the generated header never
  dereferences: `FontCache`, `UiCtx`, by-value resource handles. The Rust mirror is an opaque
  `#[repr(C)]` ZST or an existing hand-bound type, referenced by pointer.
- *`mod <name>`.* A top-level directive declaring this def depends on the hand-written **public**
  header `<name>.h` in its own `#[module]` dir. The C header emits `#include
  <flowi/<module>/<name>.h>` (ADR-0033 public include root) and the Rust binding emits `use
  crate::<name>::*;`. For the read-payload structs the vtables take by pointer: the render IR,
  `ImageDecodeResult`, the glyph/vector outlines. The type is named by its ordinary IDL name — the
  public type's `Fl` spelling matches the emitter's prefix and api_gen generates no body for a type
  it has no `struct` for, so the `mod` `#include`/`use` is all that resolves it. One import line per
  dependency, no per-type declaration.

> **Amendment.** This replaces the originally-designed *extern-include* flavor (a per-type
> `extern include <Name> <stem>` with an extern-type → header map in `naming.rs`). The per-file `mod`
> directive carried over from flowi_core's api_gen is the cleaner equivalent: it needs no type→header
> map and no verbatim-spelling flag — the existing prefix-and-don't-generate path already does the
> right thing for a referenced type the def doesn't author. `mod` covers exactly the read-payload
> include case; `extern opaque` is unchanged.

### The three-bucket type line

Every type a generated extension interface touches falls into exactly one bucket:

1. **Author into the IDL** — POD envelopes, result structs, enums, and by-value ids the interface
   owns (`FlInputDevice`, `FlPixelReadback`, the vfs result/capability structs, the by-value
   `FlString`/`FlFontId`/`FlImageHandle` ids). These become generated public headers.
2. **`mod`-provided** — read-payloads defined in hand-written public headers: the render IR,
   `ImageDecodeResult`, glyph outline, vector outline. A `mod <name>` directive `#include`s them.
3. **extern-opaque** — pointer-only deps the generated header never dereferences: `FontCache`,
   `UiCtx`, handles used only as opaque cookies.

**The render IR does not enter the IDL.** `FlRenderFrame.commands` stays `const void*`; the command
stream's record layout remains hand-written in `render_ir.h` and is reached only through the
backend's own translation code, never the generated header.

### Generate-beside-then-swap

Each interface migrates in two steps so the ABI is never in doubt: (a) **generate beside** the
hand-written header with a `_Static_assert`/`offsetof` byte-identity guard proving the generated
struct matches the hand-written one field-for-field (AFK-safe — nothing is deleted); then (b) a
separate **swap-in** that deletes the hand-written header, repoints consumers, and updates CMake
(HITL — destructive). The byte-identity guard is meaningful precisely because the seam is already
type-clean (see the `const void*` IR fact above): byte-identical *is* type-compatible here.

## Considered Options

- **Keep the extension interfaces hand-written (status quo).** Rejected: it is the same hand-mirrored
  C/Rust drift ADR-0030 introduced the IDL to eliminate, left in place for half the public ABI. A
  host links against these vtables' exact layout; an unguarded hand-written mirror is exactly the
  silent-drift hazard the IDL exists to kill.
- **Author every transitive type into the IDL (render IR, payloads, handles).** Rejected: drags the
  entire render-command record layout and FreeType/nanosvg-shaped payloads into the `.def` grammar
  for no ABI benefit — the vtables touch them only by pointer. The render IR has its own fast-moving
  internal representation that has no business being constrained by the IDL grammar; `const void*` at
  the seam is the deliberate decoupling (ADR-0001). The two `extern` flavors get the dependency
  closure to terminate without authoring it.
- **One `extern` flavor (forward-decl everything).** Rejected: forward-declaring the read-payloads
  would make the generated header fail to compile standalone for any consumer that dereferences them,
  and would lose the `#include` that a public extension header must carry to be self-contained. The
  opaque/include split is what lets the generated header compile on its own.

## Consequences

- api_gen's mandate is now "the public surface," call **and** extension. ADR-0030's "IDL = the `fl_*`
  call API, everything else hand-written" is revised: internal-only types stay hand-written, but the
  public-extension-ABI subset (the platform and VFS-driver vtables, the POD call APIs, the façade, the bitfield structs)
  becomes generated, guarded headers.
- Two durable new IDL constructs (`callback` typedef + struct function-fields; `extern`-opaque /
  `mod`) that any future public fn-pointer interface reuses.
- The render IR stays out of the IDL permanently — `FlRenderFrame.commands` is `const void*` and the
  command layout is hand-written. A future change to the IR record format touches no `.def` file.
- Each interface lands as an AFK generate-beside slice plus a HITL swap-in, so the destructive ABI
  cut is always a human-reviewed step while the codegen and guard are agent work.
- The bitfield ABI structs (`FlString`/`FlListItem`) land last, after every consumer is
  generated, retiring the hard-coded `{16,8}` `FlString` guard; the invariant
  `FlString` `{16,8}` and `FlListItem` size 32 is preserved across that switch.
</content>
</invoke>
