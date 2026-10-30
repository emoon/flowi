# flowi

The language of flowi: how UI layout becomes pixels and where the seams between the UI core
and pluggable rendering implementations sit (Rendering), plus the toolkit's widget and
interaction vocabulary (Menus, Interaction).

## Language

**Render Backend**:
An implementation that consumes the neutral render command list and turns it into pixels.
Backends are software (the default), GPU (OpenGL compute) and Metal. A backend also owns
resource preparation (images, glyphs, vectors) for its target; presentation stays host-side.
_Avoid_: Renderer (ambiguous — could mean the backend or the act of rendering)

A backend is **one coherent object** (the `FlRenderBackend` type) exposing seams that
always agree: the [[#RenderInterface]], image preparation, and glyph preparation (plus vector
and 3D preparation on backends that support them). The **host constructs it** (with its
[[#Surface Binding]]) and hands it to `ui_shared_create(arena, FlRenderBackend*)`; each window's
`ui_create(arena, UiShared*)` then draws through it. The UI core never names a concrete
backend. Concrete backends live under `src/render/` (`software`, and `gpu` with its
`gl` and `metal` implementations), never under `src/ui/`.
_Avoid_: bare `Backend`/`backend` for this object — that name is taken by the platform/host
layer (`DisplayBackend`, `InputBackend`, the `backend_*` API). The render
object is always `FlRenderBackend`.
The `FlRenderBackend` type and the render IR are internal to flowi: no public header or public Rust API names
them. Embedders choose a renderer through the `fl_app` settings (`renderer`: Software, Gpu or Metal),
never by supplying a backend; `fl_host` creates the software renderer.

**Render Command** (neutral IR):
A single high-level, backend-agnostic drawing instruction (fill rect, border, text run,
image, clip, gradient). Carries *intent* — corner radii, border widths, per-corner gradient colors,
resource references — never backend-baked data (no split corners, no SIMD shuffle masks,
no planar bitmaps). The UI core and painter emit these; each backend translates them
into its own internal form.
_Avoid_: Draw command (use "render command" — "draw command" is the GPU backend's
*internal* SDF/tile representation, not the neutral IR)

The IR is two-tier:
- **Core primitives** — fill rect (w/ corner radii), border, text run, image, clip,
  gradient. The image primitive includes a single-channel **alpha-coverage** mode (coverage +
  tint color) — the IR home for bitmap-font glyphs and alpha-only dynamic images, since a text
  run carries `FlFontId` + run and never bitmaps. Every backend must render these correctly.
- **Optional commands & effect annotations** — richer primitives and decorative effects.
  A backend MAY ignore these without breaking correctness, layout, or hit-testing. The
  SW backend skips them; the GPU backend honors them. Driven by
  data-driven visualization use cases (oscilloscopes, spectra, meters), the optional primitive set is:
  - **Polyline** — per-point array (every point carries an intensity that modulates the
    stroke; 1.0 = plain), line width, optional fill-to-baseline with its own fill color,
    open or closed loop. Caps and joins are always round — a policy, not a field. The
    workhorse for waveforms, oscilloscopes, response curves, envelope editors,
    even wireframe meshes. Updated per frame.
  - **Arc / ring / circle-sector** — knob indicators, progress rings. One shape: outer
    radius plus inward thickness; thickness reaching the center makes a filled sector.
    Angles follow the knob convention — zero at 12 o'clock, positive clockwise (ADR-0008).
  - **Circle, diamond** — graph points and nodes. Filled, with an optional
    border ring, so a selected node is a single command.
  - **Bar** — meters, spectra, bar charts. A *batched* command: one record carries
    a whole row of bars off a shared baseline. Not a fill-rect alias — batching is what
    earns the slot.
  - Decorative effects: drop shadow, glow ([[#Effect Annotation]]).
  Composite widgets (knobs, sliders, response views) are built *from* these
  primitives at the widget layer — they are not themselves IR commands.

  These optional primitives are the primary way to build data-driven visualizations
  (fast, portable), in preference to escape hatches. `dynamic_image` (CPU-rendered buffer → texture) remains as a fallback for
  the rare genuinely-custom visual.

**Layout Engine**:
flowi's owned box-layout engine — a fork of Clay (v0.14, zlib) living at `external/clay/clay.*`
(carrying `flowi divergence` markers), not a foreign vendored drop (ADR-0012, ADR-0075). It computes element
rects from sizing/padding/alignment/direction declarations; the thin `src/ui/layout.*` skin
(`ui_layout_*`) adds [[#UI scale]], pixel-snapping, scroll resolution, and IDs on top, and
is the only layer widget code talks to. The engine's per-frame output is translated into
the neutral [[#Render Command]] IR — layout decides *where*, the IR decides *what to draw*.
_Avoid_: "Clay" as a live name — it survives only as license attribution, not as a type
prefix or the engine's identity.

**Layout Id**:
The identity of a layout element — a name plus an integer **seed**. The name is hashed with the
seed to produce the element's internal id (C `Clay__HashString(name, seed)`), so the same name
with different seeds yields different elements. A plain name (seed 0) is the common case; the seed
**disambiguates repeated ids** — sibling elements emitted from one source line in a loop (timeline
lanes, list rows), which would otherwise collide. Duplicate ids are a fatal error in debug builds,
so looped elements must carry a distinct seed. The core takes **one absolute `i32` seed**; a caller
needing to disambiguate across more than one axis (row × column) **packs** those indices into that
single integer itself — the same "core takes one value, the caller composes" policy as [[#UI scale]]
(ADR-0020). In C this is the `ID(name)` / `ID_INT(name, seed)` pair; the Rust binding mirrors it as
the `LayoutId` type, built from a bare string (seed 0) or a `(name, seed)` tuple, and accepted
anywhere an element id is taken (container ids, scroll targets, the pointer/wheel queries).
_Avoid_: "key" for the seed (collides with keyboard vocabulary) and "index" (the seed is a hash
seed, not a dense 0..n ordinal — packed composites are not indices).

**Canvas**:
A region whose visual content is *custom-painted* from model data (waveforms, charts,
knob arcs, meters) rather than composed from standard widgets. A canvas is an
ordinary layout element that does **inline painting within the element's immediate-mode
scope, replayed at the CUSTOM marker** (ADR-0009): painter calls between the element's
begin and end emit [[#Render Command]]s (chiefly the optional tier) through the **painter**
API into that element's paint span, and the layout-engine seam splices the span into the
frame at the element's Clay `CUSTOM` command. Because it draws at the element's position, a
canvas:
- is positioned relative to its element's rect (coords are element-local),
- is bracketed by — and scrolls with — the live layout clip/scissor stack, like any other element,
- draws in z-order at its element's position (under its child elements), so later floating
  elements (popups, drag ghosts) compose *over* it.
The painter is therefore the *emit API used inside an element's open scope*, not a separate
end-of-frame layer drawn on top of the whole frame.
_Avoid_: "the painter layer" / "painter pass" as a single global on-top buffer — there is
no such global layer; paint is per-element and z-ordered. _Avoid_: "paint callback" — there
is no callback invoked during traversal; painting is inline immediate-mode code in the scope.

**Paint order**:
The order painted primitives composite, along two independent axes. (The layout fork reorders per-element emission so an element's
`CUSTOM` paint span comes *after* its background `RECTANGLE` and *before* its children and
border; ADR-0009.)
- **Within one [[#Canvas]] scope — call order (painter's algorithm).** Each painter call
  appends to that element's paint span in sequence, and the splice replays them in that
  exact order, so the **first call is the bottom layer and the last call is on top**. Paint
  back-to-front: backdrop, then grid lines, then content, then selection, then the cursor line.
  There is no per-primitive z key or sorting — sequence *is* z.
- **Relative to the element's own chrome and its children — paint sits between its own
  background and its children.** A canvas's paint splices in at its element's `CUSTOM` marker,
  which the fork emits *after* the element's own background `RECTANGLE` but *before* its
  children and its border. So the per-element stack, bottom→top, is: *its own `color:`
  background fill → its paint span → its child elements → its own border → later siblings →
  later floating elements* (popups, context menus, drag ghosts). A canvas therefore paints
  **over its own background but under its children, border, and anything declared later** —
  which is what lets a canvas use `color:` as a backdrop yet still sit beneath a floating menu
  (ADR-0009). The paint span is **clipped to the element's own rect**: paint that overshoots
  the element's bounds is cropped, not bled into ancestor scissors.
**`color:` is a valid canvas backdrop.** A non-transparent `color:` fills the element first
and the paint composes *over* it, so setting `color:` for a canvas backdrop works the obvious
way — no need to drop the fill and paint the backdrop by hand. The background emit is still
**alpha-gated**: Clay only emits the `RECTANGLE` when `backgroundColor.a > 0`, so a fully
transparent fill (`a == 0`, the [[#Canvas]] default when `color:` is omitted) produces no
command. A *semi*-transparent `color:` (`0 < a < 255`) tints *under* the paint (the paint
composes over it). To put something visually **above** the paint, declare it later (a child,
a later sibling, or a floating element) — never by paint-order tricks, because paint cannot
escape its own z-slot (above its background, below its children). Paint emitted outside any
open element scope is dropped (`ui_get_painter` returns null with no scope), so there is no
"draw last, on top of the frame" path.

_Canvas recipe._ Set `color:` for the backdrop (or paint it as the first call when you want a
painted gradient/texture), then paint content bottom→top in call order: grid/content, then
overlays. Put interactive or occluding parts (items, handles) as **child
elements** — they layout, hit-test, and compose *over* the paint for free. So: *color:
backdrop → painter grid → child items → later floating menus*, with no manual z
bookkeeping anywhere.

_Avoid_: assuming later painter calls in *one* element can cover a *different* element (or
that element's own children/border) — paint is pinned to its own z-slot, above its background
and below its children. _Avoid_: reasoning about paint z in window coordinates; within a scope
it is purely call sequence. _Avoid_: expecting paint to extend past the element's own rect — it
is clipped to its bounds.

**Custom painting is orthogonal to interaction.** A painted element is still
laid out, hit-tested, hovered, captured, focused, and z-ordered exactly like any other
element — by its rect and the existing input machinery. Painting geometry instead of
rects/text changes only its *appearance*. So an item, a node, or a knob is a normal layout
element (interaction for free) whose *fill* is painted; its interactive sub-parts (resize
edges, handles) are normally **child elements**. The default is always: *it is an
element*. Hit-testing geometry directly against the [[#Content transform]] is a rare,
case-by-case *performance fallback* (dense waveform samples, thousands of graph
points) — decided by measurement, never the default. DIY canvas hit-testing is historically
a workaround for retained toolkits too slow to host thousands of interactive widgets;
immediate-mode layout removes that premise here.
_Note_: the GPU/SDF technique and the optional-tier IR concern **rendering only**; they
impose no layout or hit-testing model. Those stay with the layout engine regardless of how
a thing is painted.

**Logical pixel** (authoring space):
The density-independent unit all UI is authored and laid out in. One logical pixel ≈ one
physical pixel at 100% DPI and 100% UI-zoom. Window size and DPI decide how *many* logical
pixels a surface has — a bigger or higher-DPI window yields **more** logical pixels to lay
out into (desktop-app model), not the same layout stretched to fit (the rejected fixed-
design-resolution model). Layout, widget sizes, and chrome are all in logical pixels.

**UI scale**:
The single uniform multiplier from [[#Logical pixel]]s to physical backbuffer pixels.
Applied once, uniformly, to the whole frame — every widget and every [[#Canvas]] alike.
_Avoid_: conflating it with a canvas's [[#Content transform]]; UI scale never changes how
much *content* a timeline shows, only the physical size of a logical pixel. The core holds
**zero scaling policy**: it stores one *absolute* UI scale and one *absolute* text scale
(two, so a low-resolution text boost can scale text independently of chrome), defaulting to
identity, set via `ui_set_scale(ui_scale, text_scale)` and computed by nobody inside the
core. Composition is the **app's** job: the fl_app runner reads monitor DPI
(`get_content_scale`) and a runtime UI-zoom preference (`fl_app_set_ui_zoom`, a relative
multiplier where 1.0 is DPI-correct) and pushes their product to `ui_set_scale`. See
[[#Reference fit]] for the fullscreen/TV policy and ADR-0020.

**Reference fit**:
A scaling *policy* — not a core concept — that authors against a fixed reference resolution
(1920×1080) and scales the whole UI to fit the actual surface, with a text-readability boost
at low resolutions (PAL/SD/720p). This is the game/console scale-to-fit model: a bigger
surface shows the same UI larger, not more content — the opposite of the [[#Logical pixel]]
desktop model. Correct for fullscreen TV/console apps (an embedding host application), wrong for a desktop
application. Lives entirely in the opt-in `src/ui/reference_fit.h` battery (`ui_reference_fit(height)` →
the two scales fed to `ui_set_scale`); the core never references it. See [[#UI scale]] and
ADR-0020.

**Content transform** (canvas-local):
A per-[[#Canvas]] affine mapping model/world units to [[#Logical pixel]]s within the canvas's
rect. Distinct from [[#UI scale]]: per-canvas not global, may be **non-uniform** per axis,
**anchored** (zoom around a point), and scales *content* while chrome (rulers, labels) stays at
UI scale. Applied at paint-emit time, and shared — in inverse — by hit-testing, so paint and
input agree on one mapping. Stacks beneath UI scale:
`physical_px = UI_scale · element_origin · content_transform · world_point`.
Not implemented: the code has no content-zoom transform; an app applies its own (ADR-0010).

**Placed child**:
A layout element positioned at an explicit `(x, y)` offset in its parent's **content
coordinate space** — the same space a [[#Scroll Target]]'s scroll offset lives in — instead of
by flex flow (`LayoutPlaced`). A placed child **scrolls with** its container and is **clipped
to** its active area (it is a flowed child for scroll/scissor purposes), but **does not advance
the flex cursor** and **does not contribute to content extent**: it is pure placement. The app
owns content extent, scroll-clamping, and **virtualization** (emitting only the objects in the
visible range); engine offscreen culling is the backstop. A placed child is a normal
interactive element (`item_behavior`, focus, hit-testing all apply) — the substrate for
coordinate-driven surfaces (timelines, plots, gantt bars, node graphs). Offsets are in
already-zoomed pixels (the app bakes content zoom into the coordinate). ADR-0036.
_Avoid_: "floating" for this — [[#Canvas]]/floating is an *overlay* that pins to its anchor
and overflows; a placed child is *content* that scrolls and clips with the view. _Avoid_: the
`fixed!(gap)` spacer trick as the positioning model — it is 1D-only and cannot express overlap.

**Background**:
The image (or clear color) drawn behind all UI, with a stateless two-image *blend*. The
blend/alpha/clear state (`FlBackground`) is handed across the [[#Render Command]] boundary as
background *state* at the front of the frame. The blend is generic and free:
`set_blend(prev, current, t)` mixes two images and flowi keeps no transition state. Crossfade
*timing* (delays, fade duration, advancing a slideshow) is an app concern, not flowi's. Each
backend renders the state its own way; the state crossing the boundary is the contract, not a
fixed command decomposition.
_Avoid_: a stateful crossfade machine in flowi

**Effect Annotation**:
A decorative, additive property on a render command (drop shadow, glow; `FlEffect`) that
affects appearance only — never geometry or hit-testing. Backends that don't support it skip it.

**Viewport3d**:
An element whose content is a 3D scene the app submits per frame — a view-projection matrix
plus depth-tested triangle and line draws — rendered by the [[#Render Backend]] into the
element's own color+depth target, which the UI then samples as an ordinary image
(element-local, clipped, z-ordered, dockable, multi-instance). The 3D counterpart of a
[[#Canvas]]: same "content the app draws itself" role, but geometry submitted to the backend
rather than [[#Render Command]]s emitted into a paint span. Lighting, materials and cameras are
*not* flowi's: shaders are fixed and unlit, and camera math, picking and gizmos live in the app
(gizmos are just line draws). Needs a backend with a 3D path (GPU) — a backend without one
draws a placeholder (ADR-0060).
_Avoid_: "3D canvas" (a canvas paints 2D render commands), "scene view"/"3D widget"

**Mesh**:
A retained geometry resource (`MeshHandle`) — vertices in one fixed format plus indices —
created once and drawn from any [[#Viewport3d]], as triangles or as lines depending on how it
was created (the topology is fixed for its life, because the index array only means one of the
two; line meshes are how wireframes, voxel boxes, gizmos and camera paths are drawn).
Backend-shared, like other retained resources (ADR-0011), never owned by a viewport. Textures
for a mesh are ordinary images, not a separate resource kind: an [[#Image Registry]] handle
created as a **dedicated image** (ADR-0061).
_Avoid_: Model, geometry, vertex buffer (the last names the backend-private
[[#Representation]])

**Dedicated image**:
An image created with `Image::create_dedicated`, which keeps it out of the shared image atlas,
so its handle always resolves to a whole texture of its own rather than a sub-rect. Required
for anything a [[#Mesh]] samples, because mesh UVs address the whole texture. Set at creation,
never inferred from size (ADR-0061).
_Avoid_: "unatlased"/"standalone texture"

**Resource Handle**:
A stable, opaque UI-level identity for a backend resource — `FlImageHandle` for images,
`FlFontId` for fonts. Survives frames, cache eviction, and reload. Render commands carry these
handles; each backend keeps a *private* map from handle to its own representation. The handle
namespace never changes when backends are swapped.
_Avoid_: Resource id, texture id (those name backend-private representations, not the handle)

Querying a handle (e.g. the public image query) returns only status + logical
dimensions — never pixel data, stride, or format. Those are [[#Representation]] and stay
backend-private.

**RenderInterface**:
The renderer seam of the backend vtable (`FlRenderBackend`, `src/ui/render_backend.h`):
`change_resolution`, `render` (consume a render frame), `destroy`, plus the surface lifecycle
(`create_surface`/`destroy_surface`) and optional `read_pixels`. Deliberately excludes
presentation. `render` and `change_resolution` act on a [[#Surface]] — one backend drives many
windows; resource preparation (atlas, images) and shaders are backend-shared, not per-surface.
_Avoid_: putting surface/presentation concerns here

**Surface Binding**:
How a backend is connected to its output, kept *out* of the [[#RenderInterface]] and
handled at construction + backend-specific calls the host makes (SW: CPU pixel buffer; GPU:
graphics context, e.g. an EGL or GL context on Linux, a bare `MTLDevice` on Metal). The host
owns windowing/display integration and knows which backend it built, so this asymmetry is
acceptable.

**Surface**:
A per-window render target a [[#Render Backend]] draws into and presents: its dimensions and
any size-derived targets/buffers (GPU: the offscreen RGBA8 tile-raster target, its readback
FBO, the per-tile classification buffer; SW: the output pixel buffer). **One backend owns
many surfaces** — the main window, a detachable instrument window, plugin-editor windows —
and they all share that backend's prepared [[#Representation]]s (glyph atlas, image cache)
and shaders, created once and never tied to a swapchain. `render`/`change_resolution` act on
a surface; resource preparation does not.
_Avoid_: tying shared resources (atlas, image textures) to a window/context — that is the
single-window assumption multi-window must not bake in (ADR-0011).

**Window** (host-level):
A top-level OS window the host opens, pairing an OS window (`FlWindow`, the window backend's
opaque handle) + its display/input seams + one per-window [[#UI Context]] + one [[#Surface]]
(`FlAppWindow` in `fl_app`). It is the host-level identity the runner ticks; one
[[#Render Backend]] drives the surfaces of many windows (ADR-0011). A Window is the
**OS-window backing** of a [[#Detachable Panel]] — promoting/demoting a panel swaps its
backing without the panel's emit code changing.
_Avoid_: conflating with [[#Surface]] (the render target *inside* a window) or with
[[#UI Context]] (the immediate-mode state *for* a window).

**Detachable Panel**:
A backing-agnostic unit of UI (a stable id + an inline content-emit scope) the [[#Docking]]
layer may show docked inside a [[#Workspace]], in flight on the drag overlay, or **promoted** to
its own OS [[#Window]]. The emit code is byte-identical in every backing; the choice is docking policy,
never branched on by app code. The app re-declares the panel each frame (immediate-mode) by its
stable id; the docking layer binds that id to a retained [[#Dock Node]] and keeps the panel's
state across promote/demote (the shared persistent store). The declaration scope also **places**
the content: it opens an element attached to the tile the panel currently occupies, so the app
writes the panel's widgets wherever it likes in its frame and they land where the dock put them —
declaration order is not screen order. A panel emits when it is the selected tab of its leaf, or
while it is being dragged (the drag overlay is its tile); an unselected background tab emits nothing.
A panel may carry one optional **dock-family tag** (default *general*) gating which
[[#Workspace]]s accept it.
_Avoid_: app code asking, or depending on, whether a panel is currently an OS window; persisting
the panel's geometry app-side (the [[#Dock Tree]] owns it).

**Docking**:
The framework-level policy layer that arranges [[#Detachable Panel]]s into tiled/tabbed layouts
across one or more OS [[#Window]]s. Built *on* the public substrate (Clay layout, the
[[#Window Backend]], splitters, tab bars, the shared persistent store) — it sits above the
[[#UI Context]], never inside the widget core. The unit of docking is the [[#Dock Tree]] (an
internal tiling), **not** the OS window; an OS [[#Window]] merely *hosts* a tree. Multi-window
is a transparent promotion target, so panels detach to / dock across real OS windows for
multi-monitor work without the panel's emit code changing.
The **grab gesture** is engine-owned, not app-driven: every non-empty leaf carries a tab header (a
one-panel leaf included — the header is also its grab handle), and pulling a tab a few pixels out of one
undocks that panel and arms drag-to-dock (a reserved-slot preview on the target, plus an overlay carrying
the panel's live content at the cursor), the release committing the drop and Escape cancelling it. It runs
inside the workspace's own emit, so every app and binding gets it without calling a drag API — and every
structural step (the undock, the commit, the cancel) lands at the emit's safe point, never mid-walk.
Promotion crosses the OS-window seam through an **injected host vtable** (`FlDockHost`): flowi owns
the *decision* (when to promote), the workspace↔window *lifecycle*, and *demote-on-close*; the
runner provides the window *mechanics* (open/close), so flowi never opens a window or names a global
coordinate (ADR-0041).
_Avoid_: putting the docking *trees* in the per-window [[#UI Context]] (the set of trees lives in
the shared tier — only a non-owning pointer to *which* tree a window hosts sits on the context);
branching panel emit code on docked-vs-floating-vs-promoted; calling the window backend from the
docking core (the host vtable is the seam).

**Dock Tree / Dock Node**:
The retained structure a [[#Docking]] [[#Workspace]] arranges. A **Dock Node** is *exactly one
of*: a **split** (two children, a split axis, and one ratio scalar in (0,1)) or a **leaf** (a
tab group of one-or-more [[#Detachable Panel]] ids + the selected tab). The tree stores
**topology + ratios only — never pixel geometry**: each frame the docking layer walks the tree
and emits nested Clay containers (split → a directional container with `sizing_percent(ratio)`
children; leaf → a tab bar + the selected panel's emit scope), and **Clay computes every rect**.
Node rects are read back by id (`ui_get_element_state`) for drag-dock hit-testing. A tree belongs
to one [[#Workspace]] and is hosted by one [[#Window]] — it does **not** span windows. What is
app-global is the **set of trees**: every tree lives in the **shared tier** (so it survives a
promote/demote and is *not* held in the per-window [[#UI Context]]), keyed by panel stable id. A
single-window app has exactly one; **promoting** a [[#Detachable Panel]] adds a second tree (its
own [[#Workspace]]) hosted by the new OS window, and demote dissolves it back. Structural
changes are applied as **deferred requests** at one safe point, never mid-walk. Invariants
(closing an empty leaf collapses it and dissolves its single-child split; the **root** is the
only node exempt — it degrades to an empty leaf, the floor; ratios stay clamped; each panel id
sits in at most one leaf) make every possible "hole" *topological*, since Clay makes a geometric
gap impossible. There is **no central node** (Clay `sizing_grow()` gives "absorbs remaining
space"; a never-closing app panel gives a stable workspace area).
_Avoid_: storing `Pos`/`Size` on a node (Clay owns geometry); a node that is both split and leaf;
a single-child split; a second, eased copy of the ratios for Clay to consume — the tree is what
is emitted, and every structural change lands whole on the frame it is applied (ADR-0039).

**Workspace**:
A [[#Dock Tree]] together with its host. A Workspace is hosted either by a top-level OS
[[#Window]] or **embedded as a Clay region inside a larger layout** (so a side panel can be a
Workspace occupying a sub-rect of the main window, not a separate window). It may carry an
optional **accepted-dock-family filter** (`Dock::set_family_filter`); the [[#Docking]]
drop-resolution lights a dock target only when the dragged panel's family is accepted, so an
incompatible drop never highlights (enforced in the engine, not rejected app-side after the
fact). Default filter (0) accepts any family.
_Avoid_: equating a Workspace with an OS [[#Window]] (a window hosts a workspace; a workspace can
also be an embedded region); enforcing family constraints app-side after a drop is offered.

**UI Context** (split: per-window vs shared):
The immediate-mode core state (`FlUiCtx`), split across two lifetimes so multiple windows can be
live at once. **Per-window** — one per [[#Window]], because Clay holds exactly one layout tree
per context and each window lays out a different tree: the Clay context, input snapshot,
hot/active, keyboard focus, [[#UI scale]]/DPI, popup/menu/nav stacks, the frame arena.
**Shared** (`UiShared`) — one per app: the [[#Render Backend]], font cache, [[#Image Registry]],
base [[#Theme]], icons, and the **persistent id-keyed widget state** ([[#Element State]], scroll
offsets, text-input, tree expansion) keyed by stable id — so a [[#Detachable Panel]] keeps its
state across a promote/demote (the state never moves).
_Avoid_: putting persistent id-keyed state in the per-window context (it would reset on detach);
putting layout/input/focus in the shared store (Clay can't share one tree across windows).

**Window Backend**:
The neutral injected seam for desktop OS-windowing, parallel to [[#Render Backend]]: one object
(`FlWindowBackend`) the host constructs and the runner drives, so the toolkit names no concrete
windowing library (SDL/native). **One backend owns the OS connection and creates per-window
objects** (`create_window` → an OS window handle), mirroring how [[#Render Backend]] owns shared
resources and creates [[#Surface]]s. It is the **single OS-integration seam**: window lifecycle,
the event pump (demuxed per window by OS window id), keyboard/mouse/[[#Cursor]]/relative-mouse/
warp, per-window drawable size + content scale, and **presentation** (presentation stays out of
[[#RenderInterface]], host-side per ADR-0001/0013). It is the **single desktop seam for both
renderers**: it presents a GPU [[#Surface Texture]] *and* a software CPU pixel buffer, the three
modes (`FlWindowBackendMode`: GL, Software, Metal) chosen by how the backend was constructed, so
windowed-GPU and windowed-software share one seam. Each host-level [[#Window]] pairs one Window
Backend window + one [[#Surface]] + one per-window [[#UI Context]].
_Avoid_: overloading the embedded `DisplayBackend`/`InputBackend` — those are the single-fullscreen
host video-plane path and are **host-implemented, not shipped by flowi**: flowi keeps only the
generated `DisplayBackend`/`InputBackend` *interface* (the vtable a host implements against) and
no desktop Display/Input implementation of its own. _Avoid_: putting present in
[[#RenderInterface]].

**Surface Texture** (GPU host-composite seam):
The GPU backend's host-facing accessor to a [[#Surface]]'s offscreen RGBA8 target — its
handle plus dimensions — so the host can sample the UI layer (premultiplied coverage
alpha) and composite it over its display plane, then present. A **backend-specific call**
(`gpu_renderer_surface_texture`, returning `GpuSurfaceTexture`), deliberately *outside* the
neutral [[#RenderInterface]] vtable, keeping presentation host-side ([[#Surface Binding]],
ADR-0001). The handle is a `u32` GL name on GL, an opaque `void*` `MTLTexture` handle on
Metal — **same accessor, same neutrally-named field `handle`** (backend-owned, valid until
the surface's next `change_resolution`); never a GL/Metal type, so no graphics-API type
leaves the backend header (ADR-0055). Because that field is `u32` on GL and `void*` on
Metal, `sizeof` the struct is platform-conditional; it is a value consumed locally by the
same compile unit and **must not be `memcpy`'d across differing-SDK compile-unit
boundaries**. Hand-written, not api_gen-generated, so it is *not* a layout-frozen plugin ABI
under ADR-0043.
On Metal a **second, additive accessor** (`gpu_renderer_surface_iosurface`) hands back the
same target's backing **IOSurface** as an opaque `void*` for an *out-of-process* host to
sample cross-process; the primary MTLTexture accessor coexists per-surface with the same
linear-premultiplied contract (ADR-0055 IOSurface amendment). The first IOSurface call on a
surface rebuilds its target, so it invalidates any MTLTexture handle fetched earlier — fetch
the IOSurface first. It returns NULL if the IOSurface-backed allocation fails; the surface
keeps rendering and the host falls back to the primary accessor.
The **host owns the compositor** (clear, display plane with dest rect + per-plane alpha, UI
over it) and any composited [[#Pixel Readback]]; the backend never draws the display plane.
In this repo the seam is consumed by the `fl_app` runner's windowed GPU path
(`app/app_gpu.c`) and by the standalone SDL example in `examples/gpu_window` — each a minimal
clear → sample → fullscreen-quad → swap, no display plane. `fl_app` selects its backend via
`FlAppSettings.renderer` (`Software` default, `Gpu`, or `Metal`); its headless GPU path
drives an EGL-surfaceless context so the same Surface Binding is CI-testable (ADR-0013).
_Avoid_: adding a present/composite slot to the [[#RenderInterface]] — presentation is
never in the vtable.

**Pixel Readback**:
An *optional* backend capability to retrieve rendered pixels to CPU memory (smoke tests,
screenshots) — SW returns its buffer, GPU does an explicit readback. Optional, not part
of the per-frame present path.

**Representation**:
A backend's private, prepared form of a resource behind a handle — SW: planar padded
deinterleaved pixels in a TLSF cache; GPU: an atlas rectangle / texture. For glyphs, the
GPU representation is a zoom-regenerated glyph atlas (see [[#Glyph Outline]]). Never
crosses the IR boundary. The pixel store is **owned by the backend**, not the core — the
SW pixel pool (and its eviction / on-demand reload) lives inside the SW backend; the core
never allocates or reaches into it.

**Image Registry**:
The core's backend-agnostic record of each image handle's readiness and logical
dimensions (`image_registry_is_ready` returns readiness plus width/height) — the single
source the public image query and [[#Layout Engine]] read. It holds **no pixels**: realized
pixels are a backend [[#Representation]]. Presence in the registry *is* readiness: an image
becomes **ready** when the active backend has *realized* it and acks it via
`image_registry_mark_realized` (see [[#Resource Readiness]]). Pending and error states are
not tracked here — they stay in the load/decode path. Backend-owned pool pressure (SW
eviction + reload) is invisible here — a once-realized image stays "ready," and a
transient pool miss self-heals inside the backend.
_Avoid_: treating the registry as an image cache — it stores readiness + size, never
pixels; the pixel store belongs to the backend ([[#Representation]]).

**Font (shared layer)**:
The backend-agnostic font handling: FreeType face management, metrics/sizes, shaping,
glyph indices, and glyph-outline access. It **never rasterizes** — rasterization is a
backend concern. The SW backend privately calls FreeType's rasterizer (hinted
grayscale/LCD); the GPU backend never CPU-rasterizes and renders outlines on the GPU.
Layout and text measurement depend only on this layer.
_Avoid_: Font cache (overloaded — historically also did rasterization)

**Text Overflow**:
How a single-line text run is displayed when its natural width exceeds the box it must fit in.
The core text layer owns two **stateless** modes, selected by `TextConfig.overflow` (the
`Overflow` enum) and decided per frame as a pure function of (text, box width, font): **clip**
(the default — the run is hard-cut at the box edge and the remainder scissored away) and
**elide** (the run is truncated to the longest prefix that fits and an ellipsis "…" marks the
omission). Eliding requires the run to be **width-constrained** by its box — selecting elide
implies that constraint, since with an unconstrained (grow-to-content) run there is nothing to
elide against. This is the sibling of [[#Logical pixel]]-space text wrapping: all three
(wrap / clip / elide) are stateless display policies on the text config.
_Avoid_: treating a **marquee** — an *animated* scroll (wrap-around or ping-pong) of overflowing
text — as another overflow mode alongside clip/elide. A marquee is a different, **stateful**
category: a per-element offset advanced over time (forcing continuous redraw), so it belongs to a
stateful text *widget* keyed by id — see [[#Marquee]] — never to the stateless text config.
_Avoid_: "truncate" as a synonym for elide — truncation omits the ellipsis signal.

**Marquee** — _designed, not yet implemented (ADR-0069)_:
An id-keyed **stateful text widget** that animates a single-line text run horizontally through a
width-constrained box the text does not fit in — the animated sibling of the stateless
[[#Text Overflow]] modes, and deliberately *not* one of them. Two **motions**:
**wrapping scroll** (the run scrolls leftward continuously and re-enters after a visual **gap**,
ticker-style) and **ping-pong scroll** (the run scrolls to its far end, pauses, and reverses,
pausing at both ends). Motion follows a fixed **phase cycle** — start delay → scrolling (constant
speed, stated in [[#Logical pixel]]s per second so perceived speed is scale-independent) →
end pause → wrap or reverse. Identity is a stable id **separate from the text**: the text is
display payload, and a text change resets the motion to the start of the cycle (a new title is
read from its beginning). When the text *fits* its box — including the not-yet-metrics-ready
frame under [[#Resource Readiness]] — the marquee degrades to static text at rest; it never
animates text that is fully visible. The box's width must be an **explicit sizing choice**
(a fixed logical width or grow-to-parent); a marquee with no width constraint is a caller error,
not a silent never-scrolls widget. Scrolling is either unattended (the default) or armed only
while hovered.
_Avoid_: "marquee mode" on the text config (it is a widget, not a config value — the
[[#Text Overflow]] boundary); "autoscroll" (collides with drag-to-edge list autoscroll); confusing
it with the list's rubber-band **marquee selection**, an unrelated pointer gesture that shares the
name.

**Resource Readiness**:
An async-loaded resource (font, image) is not ready the instant it is requested — its bytes
are read off disk asynchronously, and a frame MAY render before the load resolves
(**render-when-ready**: the core never blocks on a load). A font has **two** readiness levels:
**metrics-ready** (FreeType face + sizes available, so the [[#Layout Engine]] can measure
text) and **raster-ready** (glyphs realized for a backend — SW coverage bitmap or GPU atlas).
Layout depends only on metrics-readiness; drawing depends on raster-readiness, which stays
render-when-ready (a not-yet-rasterized glyph is retried next frame, never waited on). An
**image** has one readiness level: it is ready when the active backend has **realized** its
[[#Representation]] (SW pixels in the pool, GPU texels in the atlas) — *ready means realized*,
not merely decoded. The active backend signals realization; the core records readiness +
dimensions in the [[#Image Registry]], reported the same way whichever backend is live. An app
MAY **opt in** to a **load barrier** (`fl_font_ensure_loaded`) that pumps I/O until a chosen set
of resources is ready *before* the first rendered frame — to avoid first-frame reflow and to
keep pixel goldens deterministic — but this is *app policy*; the core never *requires* a wait.
The barrier waits on a subset (today: all pending fonts, since fonts are the layout-critical
resource); other loads keep streaming in the background.
_Avoid_: "loaded" as a single boolean for a font — it conflates metrics-ready (layout-usable)
with raster-ready (drawable); they resolve at different layers. _Avoid_: treating a transient
not-ready measurement as a permanent fact (it must never be cached as the element's size).

**Glyph Outline**:
The vector contours of a glyph obtained from FreeType without rasterizing. The shared form
handed to a backend, which prepares it as it sees fit: the GPU backend renders outlines
into a glyph atlas on the GPU (regenerated when zoom/scale changes); the SW backend
rasterizes them to a sized coverage bitmap. Rasterization stays on the GPU because it is
far faster there.

**Vector Image**:
An SVG resource prepared as resolution-independent vector geometry rather than a fixed
bitmap — the icon analog of a [[#Glyph Outline]]. A shared vector layer parses the SVG once
(via nanosvg) into a neutral form — per-shape contours, each with a solid fill color and
fill rule — and **never rasterizes**; a [[#Render Backend]] prepares it as it sees fit. The
GPU backend rasterizes the contours into a zoom-regenerated **SVG atlas** (so icons stay
crisp under zoom without CPU re-rasterization); the SW backend rasterizes to a bitmap.
A vector image crosses the IR as an ordinary image ([[#Render Command]] image primitive +
[[#Resource Handle]]) — never a new command or handle. **Single-color** vector images use the
image primitive's alpha-coverage mode (coverage + draw-time tint, recolorable like a glyph);
**multi-color** ones bake their per-path solid fills into a color cell.
GPU-vector is a GPU-backend-only capability: only **solid-fill** SVGs (single-color or
flat multi-color) take the vector path. SVGs carrying **strokes or gradients** are
**rejected at load** on the GPU path (logged, not drawn — the asset pipeline must produce a
supported SVG); the GPU backend never CPU-rasterizes an SVG. The **SW backend is untouched**:
it keeps its existing CPU `decode_svg` (nanosvg→RGBA8) and renders SVGs as today.
_Avoid_: treating an SVG as a distinct resource kind — it is an image whose backend
[[#Representation]] happens to be vector. _Avoid_: a CPU-raster fallback inside the GPU
backend — unsupported SVGs are rejected there, not rasterized.

_Supported subset (GPU vector path)_: solid-color fills only — single-color (monochrome,
tintable) or flat multi-color (per-shape solid fills baked into a color cell); both
`nonzero` and `evenodd` fill rules; per-shape opacity. **Not** supported: any gradient fill
(linear or radial) or any stroke. Rejection is per-image: a single gradient/stroke shape
rejects the whole SVG (it is logged at warning and not drawn — no missing icon ever appears
silently). Parse failure, empty/whitespace-only, and zero-solid-fill SVGs reject the same way.

_Asset pipeline_: a curated icon meant for the GPU vector path must ship as solid fills with
no strokes. Pre-convert strokes to fills before committing the asset — Illustrator's *Object
→ Path → Outline Stroke*, Inkscape's *Path → Stroke to Path*, or `svgo` — and replace any
gradient with a flat solid color (or, if the gradient is essential, keep the asset on the SW
raster path rather than the GPU vector path).

**Design Token**:
A named, themeable design constant a widget references instead of a literal — either a
semantic color slot (see [[#Theme]]) or a step on the spacing/radius/sizing scale
(`design_tokens.def`). Widgets read tokens, never raw values.
_Avoid_: constant, magic number, hardcoded color.

**Theme**:
The active set of semantic color slots the UI renders against — organized as
interactive / surface / border / text / feedback. The app supplies the resolved palette with
`fl_theme_update` (flowi falls back to a built-in neutral default palette until it does);
`fl_theme_push` / `fl_theme_pop` layer a sparse `ThemeOverride` onto a subtree, and a widget's
`theme_override` layers one onto that widget alone. Widgets read semantic slots, never raw
colors. Interactive state colors (hover/press/focus) and per-object color variants are derived
perceptually in OKLCH, not by RGB lerp (ADR-0015).
_Avoid_: skin, palette, style sheet.

**Theme Preset**:
A complete set of [[#Design Token]] values an app supplies on top of the framework
baseline. The same widget kit wears many presets, differing only in data, never in code
(ADR-0014). flowi owns the token system, the OKLCH derivation, and one neutral default
preset; apps own the rest. Loading a preset from a `theme.json` and a density setting are not
implemented in this repository — a preset is app-supplied data passed through the [[#Theme]]
API.
_Avoid_: theme variant; reserve "mode" for light/dark *within* a preset.

**Object Color**:
An app-assigned base color on a domain object, distinct from the
[[#Theme]]'s semantic slots. *Assignment* — which object carries which base, and its
persistence — is app-side (ADR-0014); the framework only *derives* from a base it is
handed. The base flows through the same [[#Widget Prologue]] as a themed widget, so its
interactive states are perceptual too (ADR-0015).
_Avoid_: user color (ambiguous with theme), custom color.

**Perceptual Variant**:
A framework-derived sibling of a base color — lighten / darken / desaturate in OKLCH —
plus the luminance-chosen **label color** (a readable foreground picked between the
theme's light/dark text by the base's OKLCH lightness; `color_readable_foreground`). Derived
from an [[#Object Color]] or a [[#Theme]] slot; the framework exposes the perceptual
primitives and the app composes the set it needs (ADR-0014).
_Avoid_: shade/tint (implies an RGB lerp); state color (reserve that for the interactive
hover/press/focus family, a different axis).

**Widget Prologue**:
The uniform opening every interactive widget runs before it lays itself out, in one fixed
order: resolve its [[#Theme]] (with any per-widget slot override merged in), resolve its
identity, dispatch the shared interaction behaviour, look up its persisted interaction
state, and derive its state-dependent colours. This *is* flowi's "what is a widget here?"
contract — one answer at one address (`ui_widget_prepare`), not the
same five steps copy-pasted into each widget. It returns its result by value as a
`UiWidgetState`: the resolved id, this frame's interaction signal, a handle to the widget's
persisted interaction state, its resolved theme, and its derived state colour set. The
genuinely-uniform core is the identity + behaviour + persisted-state + variant-colour skeleton;
widgets that carry a **toggle fill** (checkbox, switch) run the same prologue and then blend
their own two-state fill on top — easing it through the *one* shared toggle animation
(`anim_toggle_toward`) so a checkbox, a switch, and a radio slice all animate identically
under the same [[#Theme]]. The **multi-item selection** widgets (radio group, tab bar) are a
deliberate exception: they run a *two-pass* structure — resolve the selection across all items
*before* any item lays out, to avoid a one-frame selection lag — so their behaviour and colour
passes are split and they share the colour/animation helpers directly rather than the bundled
prologue.
_Avoid_: "widget begin" (implies a matching close and an open scope — the prologue returns
a value, it never opens a bracket); re-deriving any of the five steps inline in a widget.

## Foundation boundary

**VFS Driver**:
A filesystem backend the [[#Layout Engine]]'s asset path resolves through — local disk, an
archive, a network store. flowi's `vfs` module owns the **driver interface** — the `FlVfsPlugin`
struct (identity + lifecycle + the operations vtable: `open`/`read`/`get_size`/`list_directory`/
`close`/`get_capabilities` plus optional ops) — and is **handed already-constructed
drivers** via `vfs_register_driver`; it never loads them. flowi's vfs is always *static* in this
sense, so a driver's `create` takes no arguments — flowi never wires services into a driver
at construction (ADR-0034).
_Avoid_: treating dynamic `.so` loading or a service registry as a flowi concern. Turning a loaded
library into a driver is the embedding application's job — flowi only ever sees a constructed
`FlVfsPlugin`.

**VFS Mount**:
A tree of nodes that resolves paths for every operation made through it. The root node carries the mount's driver
and one handle; a path may pass through any number of archives (`dir/a.zip/b.zip/sub/c.lha/file`), each of which is
opened once, on demand, and becomes a node carrying its own driver and handle. A path is handed to the nearest
node that carries a driver, so callers use plain paths and never mount each archive (ADR-0074). A nested path can
also be mounted directly. One lock per mount guards the tree and every driver call; listings flag archives by name
at every level.
_Avoid_: per-worker handle copies, a lock for the tree separate from the driver lock, mounting each archive by hand.

**VFS Operation Handle**:
The opaque `u32` ticket (`FlVfsHandle`) for one in-flight or completed vfs operation — a read,
open, or listing — distinct from a [[#Resource Handle]], which names a UI-level resource. It names a
reference-counted operation in the VFS slot table. Its result is readable from the moment the
operation is ready until `fl_vfs_close`; close unlists the id and never blocks (a `fl_vfs_read` handle,
which lends the caller's buffer, is the exception), and an operation still running is destroyed
when its job ends (ADR-0074).
_Avoid_: "file handle" (an operation, not a file, may hold one), reading a result concurrently
with closing the same handle — that is caller error, not a supported pattern.

**ABI String**:
A **non-owning counted view of bytes** — a pointer, a length, and two flag bits (`is_static`,
`is_ascii`) packed beside it — that is the shape every string takes crossing flowi's C ABI.
Spelled `FlString` in C and `RawStr` in Rust. It **owns nothing and guarantees no lifetime**: how
long its bytes stay valid comes entirely from whoever produced it — a `'static` literal, the
caller's stack for the duration of one call, an arena (the per-frame one or any other), or the
host's own contract for a returned value. Nothing in the value itself records which, so keeping
one past the call that produced it is an obligation the holder takes on by hand. flowi's own safe
APIs never hand one to an application: they take and return ordinary strings and pack the view
internally.
_Avoid_: "frame string" — the per-frame arena is only one of the four ways one of these is kept
alive, and naming the type after it invites storing the other three as if they were frame-scoped
(ADR-0027 amendment). Also avoid treating it as a string *type* — it is a view of bytes that need
not be UTF-8, which is why reading one out is lossy rather than fallible.

**SID**:
A `SID_`-prefixed string a widget receives in place of display text — the key naming a translation,
resolved against the active [[#String Table]] where text enters the display path. The whole string
is the key: one SID names one complete label or sentence, translated as a unit. Because resolution
happens at display time, widget identity is derived from the SID itself, never the translated
text — a [[#Language Switch]] cannot change any element's identity.
_Avoid_: stitching several SIDs (or a SID plus prose) into one string in code — sentence structure
varies by language, so a message with runtime values is one key carrying [[#Indexed Placeholder]]s,
never fragments concatenated at the call site. Also avoid assembling a SID at runtime where a
literal would do — a constructed SID is invisible to static drift tooling, leaving the runtime
missing-key warning as its only check.

**Indexed Placeholder**:
The `{0}` / `{1}` positional slot a localized string carries where a runtime value is substituted.
Indexed, never printf: translations may reorder slots freely across languages, and table data is
never interpreted as a format string. A literal brace is written `{{` / `}}`. A slot with no
matching argument renders literally with a warning — degrade visibly, never crash.
_Avoid_: "format specifier" — a placeholder carries no type; it renders whatever value fills it.

**Plural Variant**:
One of up to six alternate forms of a localized string (`zero`, `one`, `two`, `few`, `many`,
`other` — the CLDR category names), selected at resolve time from an explicit count by
per-language plural rules. Only `other` is required; a missing variant degrades to `other`, then
to the default language.
_Avoid_: a two-form singular/plural model — Slavic languages need `few`/`many`; the variant
vocabulary exists precisely so the schema never assumes two forms.

**String Table**:
One language's translations: a flat map from full `SID_`-prefixed keys to either a plain string
or a Plural Variant object, shipped as one JSON file per language (JSON5 accepted on read).
Malformed entries are skipped with a warning and degrade through the missing-key fallback chain,
exactly like an untranslated string; only unparseable JSON rejects a file. At most two tables are
resident: the active language and the default.
_Avoid_: keys without the `SID_` prefix — exact string identity between code and table is the
contract; nothing strips or prepends on either side of the lookup.

**Language Manifest**:
The small JSON file that makes a shipped translation set self-describing: it enumerates the
available languages (lowercase BCP-47-style tag, optional native display name, explicit table
path resolved relative to the manifest) and designates the default/fallback language. flowi is
pointed at the manifest; languages are never inferred from directory contents or filenames.
_Avoid_: treating the default language as an app setting — it is a property of the translation
set (the language complete by construction), recorded in the manifest that ships with it.

**Language Switch**:
Changing the active [[#String Table]] at runtime. Requested at any time, applied only at a frame
boundary: every frame renders entirely in one language, and text resolved during a frame stays
valid because a replaced table outlives the frame that last read it. A switch never blocks — the
old language stays active until the new table is ready, and a failed load leaves the current
language in place with a warning. Reloading a changed table from disk (hot-reload) is the same
switch pointed at the same tag.
_Avoid_: treating a switch as instantaneous mid-frame — a half-and-half frame is exactly the
failure mode the boundary rule exists to prevent.

**Service API** (foundation):
flowi core's foundation — arena, string, log, path, heap, VFS — exported as **plain C symbols**
from the shared library (`fl_arena_create`, `fl_string_to_cstr`, `fl_heap_alloc`, `fl_path_normalize`,
`fl_log_*`, the `vfs_*` family). api_gen emits, from each service `.def`, the public C header, the
linker export list, and the Rust `extern "C"` declarations — there is no per-service `Fl<Svc>Api`
vtable struct, `g_fl_<svc>` global, `*_get_public_api` adapter, or `FL_PLUGIN_USE_*` macro.
Plugins link the same exported symbols, and flowi-internal callers call them directly too, so
ADR-0032's "no internal indirection" spirit holds. Each service header lives
in its own public module dir (`<flowi/arena/arena.h>`, `<flowi/string/string.h>`, …).
_History_: these were injected `Fl<Svc>Api` vtables (ADR-0046); ADR-0058 replaced them with
exported symbols once flowi began shipping as a shared library.

## Menus

**Menu**:
A floating, transient list of commands the user opens, picks one from, and dismisses. flowi
has **one menu core feeding two surfaces**: a **context menu** (opened at the cursor) and a
**menu bar** (a persistent File/Edit/View strip whose items open the same core anchored to a
bar button). A menu's contents are produced by emitting items in immediate-mode scope, not by
handing over a description — so heterogeneous items, submenus, and arbitrary custom content
(e.g. color swatches) compose by just placing widgets between items.
_Avoid_: Dropdown (the homogeneous string picker `ui_dropdown` — a simpler, separate widget; a
menu carries mixed item kinds, nesting, and custom content, and is built, not described).

**Open Path**:
The live chain of what is open in a menu right now — the root plus the one submenu expanded at
each level — held as a single ordered path under **one** focus scope. Moving the
[[#Menu Highlight]] to a different branch collapses the deeper path and opens the new one; an
action dismisses the whole path; ESC pops one level.
_Avoid_: popup stack / nested scopes (the chain is one scope with a path inside it, not a stack
of independent popups).

**Menu Highlight**:
The single item marked as current within a menu level, driven by whichever of mouse-hover or
keyboard moved last (the two always share it). It is a menu's own intra-navigation state,
separate from the application's global keyboard focus ring, which a menu scopes away while open.
_Avoid_: Focus (the tab-ring belongs to the background UI; a menu navigates by highlight, and
picking an item never moves application focus).

## Interaction

**Action** (semantic input):
A logical thing the user did — *Confirm*, *Cancel*, *nav up/down/left/right*, *next/prev* — decoupled
from the physical input that triggered it. Actions are **toolkit vocabulary**: widgets respond to them,
and the public `FlAction` enum is the stable semantic core a controller/keyboard-driven app queries
(`fl_action_pressed(FlAction)`) without owning the nav logic. The enum's discriminant order is
**layout-frozen plugin ABI** (ADR-0043).
_Avoid_: "key" / "button" for an Action — those are the physical trigger, not the semantic verb; the
mapping between them is a [[#Binding]].

**Binding** (physical → action mapping):
Which key, mouse button, gamepad button, or axis triggers a given [[#Action]]. Bindings are **app-level
input configuration**, not toolkit vocabulary: flowi ships sensible **default** bindings internally so
its widgets work bare, but the binding *types* (`InputBinding`, `InputType`, modifiers) and any
**remapping** stay internal — there is no public `fl_*` to mutate a binding. An app that wants custom
controls drives its own input layer.
_Avoid_: treating a Binding as part of the public action surface — the public face is the [[#Action]]
vocabulary; the Binding is the internal mechanism behind it.

**UI Feedback Event**:
A semantic report of *what happened* in the interaction layer — focus **moved** between widgets, a
directional nav **hit an edge**, a widget was **activated** — emitted through one app-installed hook
(`fl_ui_set_feedback_hook`). flowi reports the event; the **app owns the effect**, mapping each
`FlUiFeedbackKind` to a UI sound, haptic/rumble, screen-reader announcement, or telemetry ping. flowi
stays ignorant of all of them — naming it
"feedback" rather than "sound" keeps haptics/a11y open. The hook is NULL by default and fires only on
discrete, human-driven interactions (never per frame): zero cost until an app opts in. Events come off
the existing nav ([[#Action]]) and activation paths (ADR-0056).
_Avoid_: a "sound event" / a `play_*` vocabulary inside flowi — that re-imports the audio ownership the
library was stripped of; the effect is the app's, flowi only says what occurred.

**Scroll Target**:
An area or widget that opts into mouse-wheel routing over its own rect (surfaced to callers as a
declarative flag). The toolkit routes each frame's [[#Wheel Delta]] to the **single topmost Scroll
Target under the cursor** (the *hot* element) and to no one else — an inner Scroll Target
shadows its ancestors (*consume-takes-all*: the wheel does not bubble up to a parent the inner
target declined to use). An element that does **not** declare itself a Scroll Target sees no
wheel at all — the wheel is handed to engine-managed scrolling instead. Declaring the target is
therefore an **input-routing** act, separate from *reading* the value: routing decides who is
allowed to read, the [[#Wheel Delta]] query reads.
_Avoid_: "claim the wheel" as a separate verb — being a Scroll Target *is* the claim, there is
no second step. _Avoid_: conflating it with an engine-managed scroll area (which owns a scroll
*offset*); a Scroll Target only routes the raw delta to a reader that owns its own offset.

**Wheel Delta**:
A single frame's mouse-wheel movement — the wheel events accumulated during that frame, reset to
zero each frame. There is **no absolute wheel value**: the hardware emits deltas, and any
cumulative offset (a list's scroll-y) is the **app's** to
accumulate. Read scoped to an element id and returned only when that element is the hot
[[#Scroll Target]], so two stacked targets never both see the same tick. Shift+wheel is
axis-swapped at the input substrate (a vertical wheel lands in the horizontal component), so
horizontal-pan intent needs no modifier query; a modifier query is needed only to disambiguate
**same-axis** gestures (plain-wheel scroll vs Ctrl/⌘+wheel zoom both land in the vertical
component).
_Avoid_: "scroll amount" / "scroll position" for this value — it is a per-frame delta, never a
position.

**Mouse Button State**:
A mouse button's interaction over a single frame, read as three independent channels — *pressed*
(the edge where it went down this frame), *held* (the level: currently down), and *released* (the
edge where it went up this frame). The three are **not** mutually exclusive: a fast tap is
*pressed* and *released* in the same frame, and a just-pressed button is both *pressed* and
*held*. Unlike the [[#Wheel Delta]], button state is **global and unrouted** — every reader sees
the same state regardless of what is under the cursor, because (unlike the wheel) it is never
zeroed or handed to engine-managed scrolling, so a single frame's read is always reliable. The
queries are `mouse_pressed`, `mouse_down` and `mouse_released`.
Routing a button to one owner is therefore unnecessary at the toolkit level; an interaction that
must be *owned* — a drag — is **latched by the app**: it notes *pressed* while the cursor is over
its region, then follows *held* and the [[#Mouse Position]] until *released*, even as the cursor
leaves that region. That latch is why a pan/drag is the app's state machine, not an engine-managed
one (mirrors the [[#Scroll Target]] rationale: the app owns its view state).
_Avoid_: a single up/down value for a button's per-frame state — it cannot represent a same-frame
tap and it conflates the held *level* with the down/up *edges*. _Avoid_: "click" as a synonym —
a click is a higher-level widget [[#Signal]]-borne outcome (press-then-release on the same hovered
widget), not the raw button state.

**Mouse Position**:
The cursor location relative to an element's rect (`mouse_position(id)`), in [[#Logical pixel]]
(authoring) space — the same space a [[#Canvas]] paints in, so cursor→content math
(cursor-anchored zoom) needs no scale conversion and reads the same logical point at any UI scale. This authoring-space
reading is the **default** the app reaches for; the physical-pixel cursor position carried on a
widget's interaction [[#Signal]] (`mouse_pos`) is the exception, kept for the rare widget that works in device
pixels.
_Avoid_: "pointer" as a distinct concept — it names the same cursor; the query vocabulary says
"mouse".

**Hidden Drag**:
An *infinite*, cursor-relative drag for scrubbing an unbounded value — a knob, fader, numeric
drag-field, or timeline pan. On begin the OS cursor is **hidden** and the mouse enters
relative/unbounded mode; each frame the app reads a raw mouse **delta** (already multiplied by a
caller-supplied sensitivity **scale**) and folds it into its value; on end the cursor is shown and
**warped back to the point where the drag began**. Distinct from the ordinary bounded drag latch
([[#Mouse Button State]]): a bounded drag follows absolute [[#Mouse Position]] and **stalls when the
cursor hits a screen edge**, whereas a hidden drag never moves the visible cursor and so scrubs
without limit. The app still **owns the latch** — it decides press→begin, held→read-delta,
release→end (the same app-owned-interaction line as [[#Mouse Button State]]); the platform owns only
the hide / relative-motion / warp-back mechanics. Bracketed by `mouse_hidden_drag_begin(scale)` /
`mouse_hidden_drag_end()`, with the per-frame scaled delta read via `mouse_drag_delta()`.
_Avoid_: reading [[#Mouse Position]] or a position-derived drag delta for an unbounded scrub — it
clamps at the screen edge, which is the very thing a hidden drag exists to escape. _Avoid_: "pointer
lock" / "mouse capture" as the name — the user-facing verb is *hidden drag*.

**Cursor**:
The mouse pointer's visual shape, **requested per frame** with `fl_cursor_set`. The request carries
an opaque `FlCursor` value that today holds one of the **system shapes** (arrow, hand,
resize-H/V/diagonal, text, crosshair, not-allowed); custom image cursors (an image plus a hotspot)
are not supported yet.
The shape is re-stated every frame, **last-writer-wins, and resets to the arrow** when nobody
requests one (the immediate-mode model: a clip-edge hover asks for resize-H that frame; a persistent
tool re-asks each frame it is active). Cursor **visibility** (`fl_cursor_set_visible`) is a separate,
sticky toggle, and a [[#Hidden Drag]] hides the cursor for its whole duration regardless. Lives on
the platform input seam (the SDL/host backend), never in the layout core.
_Avoid_: a sticky "set and forget" cursor that each widget must reset on un-hover — the per-frame
request removes that bookkeeping.

**Element State**:
The per-element interaction-and-geometry record, **pulled by id** for any laid-out element
(`get_element_state` in C, `element_state` in Rust) — its rect (origin + size), whether it was
`found` in this frame's layout and is `visible`, and its interaction flags (`hot` = the cursor is
over it, plus `focused` / `active` and their animation interpolators). The dual of a [[#Signal]]: a
Signal is *pushed back* by value from a built-in widget you called, Element State is *pulled* by id
from any element you laid out — so it serves a plain area (a timeline viewport) as readily as a
widget, which is why the noun is **element**, not "widget". The C struct reports the rect in
physical pixels (divide by `get_scale`); the Rust binding reports it in [[#Logical pixel]]
(authoring) space, the same space as [[#Mouse Position]] and the [[#Canvas]] painter, so `size`
composes directly with cursor and [[#Content transform]] math and the app never sees [[#UI scale]].
The rect is the **previous** frame's resolved layout (immediate mode), so query an element that
existed last frame; a never-laid-out id reports `found = false`. The flags beyond `hot` (`focused`,
`active`, the interpolators) are set by the [[#Widget Prologue]], so a plain non-widget area reports
its rect / `visible` / `hot` / `found` but zeros for the widget-only facets. A **plain area does not
enter the interaction state on its own** — a widget enters via its own behaviour, but a bare
container must opt in by declaring itself a [[#Scroll Target]] (the timeline-viewport pattern);
until it does, Element State reports `found = false` for it.
The single topmost-hovered element across the whole frame is also queryable directly, without
re-walking every id: `hot_id()` in C (`hot_element()` in Rust) returns the winning [[#Element Id]]
(or none). It is the *pull* complement for a consumer that handles input in a pass separate from
declaration; the inline model — read `hovered`/`pressed` from each element's own [[#Signal]] — needs
neither it nor a re-walk, and is preferred. _Avoid_: looping the element state's `hot` over a whole
model every frame to find the hot one.
_Avoid_: "widget state" for this record — it is keyed by an *element* id and serves any element,
not only widgets; reserve "widget" for the [[#Signal]]-returning built-in controls.
_Avoid_: reading the rect in physical pixels from the Rust binding — it reports [[#Logical pixel]]s
to keep the app in one space with [[#Mouse Position]] and the painter.

**Element Id**:
The resolved `u32` identity of a laid-out element — the hash of its [[#Layout Id]] (name + seed),
the key under which the core tracks its [[#Element State]] and `hot`/`active` status. The hash is
**one-way**, so a consumer that wants to map a returned Element Id back to its own object hashes its
*own* candidate ids via `hashed_id` (`LayoutId::hashed_id()` in Rust) and keys a map on the result.
Surfaced as a distinct handle type (0 is invalid), not a bare `u32`, so the values returned by
`hot_id`/`hot_element` and `hashed_id` are compared type-safely. _Avoid_: persisting an Element Id
across frames as a stable handle — it is a within-frame hash, recomputed each frame; the stable
identity is the [[#Layout Id]] (name + seed).

## Lists

**List**:
A scrollable, virtualized (uniform-row-height) column of caller-emitted items with built-in
interaction *mechanics* — (multi-)selection, keyboard navigation, and drag-reorder — but **no
model policy**. It follows the Dear ImGui `BeginMultiSelect` split (and flowi's immediate-mode
app-owns-state rule, [[#Mouse Button State]]): the List runs the selection/drag *gesture
state machines* and **draws their affordances itself** (selection highlight, the drag
**ghost**, the **insertion line**) — the caller never hand-draws drag visuals — while the
**app owns the data**: what an item is, the selection set, drop validity, and how a reorder
mutates the model. This is a thin reusable reorder-list widget, a *convenience*
rather than new substrate. It reuses `list_uniform`'s uniform-height virtualization core rather than forking
a third scroll engine; variable-height rows and group headers are not supported. Reorder is
**intra-list only** (cross-list and OS-shell drag-out are not built). Exposed with **two faces**: a *declarative* face (`ui_list` — data-described
`ListItem` label/icon rows, everything internal, selection auto-applied) for the common case, and a
*scoped* face (`ui_list_begin`/`ui_list_item_begin`/`ui_list_item_end`/`ui_list_end`) for rows with custom caller-emitted content.
Distinct from a [[#Menu]] (a transient command picker that is *built, not virtualized*) and
from the bare `list_uniform`/`content_view` virtualizers, which deal only in dense indices and
carry no selection model.
_Avoid_: a "batteries-included" List that *stores* the selection set or mutates a caller
array on reorder — that bakes model policy into the framework and breaks the [[#Item Id]]
boundary. _Avoid_: "Menu" for a List (a menu is transient and command-shaped).

**Item Id**:
The two identities every [[#List]] item carries, deliberately kept separate. The **stable
id** is an app-supplied opaque `u64` naming *this item* persistently — it survives re-sort
and filtering, and is the only key the selection set and the
drag payload are expressed in; the List interprets it as pure identity, never as data. The
**position** is the item's ordinal in the current visible order, tracked automatically by
emission order; it is what range gestures (shift-select, marquee) compute over. A flat
static list MAY pass its index as the stable id and lose nothing; anything sorted, filtered,
or hierarchical must supply a real stable id. Mirrors the [[#Layout Id]] split between a
name and a disambiguating seed, but at the data layer rather than the element layer.
_Avoid_: keying selection or drag payloads on position/dense index — sorting then silently
re-selects different rows and a collapsed tree forgets its selection. _Avoid_: the List
ascribing meaning to the `u64` — identity only.

**Selection Request**:
The unit the [[#List]] hands back to describe a selection *change* — never the resolved set
(the List does not store it). A tagged `SelectionRequest` of kind `ClearAll`, `SetAll`
(`selected`), `SetOne` (`id`, `selected`), or `SetRange` (`id` .. `to_id`, `selected`). Ranges are expressed by their **two endpoint
[[#Item Id]]s**, not by enumerating the span, so a range survives virtualization and tree
collapse: the app — which alone owns the full ordered model — walks from `from_id` to `to_id`
and flips those items, including rows scrolled off-screen the List never saw. The List
auto-applies requests into the caller-owned `FlSelectionSet` it was handed (pass a NULL set to
opt out and apply them yourself, e.g. for undo-coalescing or exotic storage); the result's
`requests` array is populated either way. See ADR-0028.
_Avoid_: returning the resolved set from the List (that moves storage into the framework and
makes `SetAll`/range O(n) and un-virtualizable). _Avoid_: range-by-span (breaks the instant
the middle of the range is virtualized away).

**Selection Set**:
The caller-owned storage of selected [[#Item Id]]s (`FlSelectionSet`) — an opaque,
caller-owned handle of `u64` identity, never interpreted as data. It is the *only* place a
resolved selection lives; the [[#List]] keeps just the gesture anchor and emits
[[#Selection Request]]s into the set (auto-applied by default). Applying a request that names
a span (`SetRange`, or `SetAll`) resolves that span by **searching the app's full ordered id
list, passed in as a plain slice** — never via a stored callback or adapter. So expanding a
range across off-screen/virtualized rows is *data the app hands to `apply`*, not code the set
holds: the slice is the model's enumeration, and the trivial flat case (id == position) needs
no slice at all. A missing endpoint applies to nothing (debug-assert). Requests apply in array
order — sequence *is* semantics (`ClearAll` then `SetOne(x)` nets to `{x}`), the same
"sequence is z" rule as [[#Paint order]].
_Avoid_: a callback/adapter the set stores to resolve ids (the Dear ImGui shape) — the app
owns the model and passes its ordered ids as a slice, keeping the FFI seam callback-free.
_Avoid_: the set storing or interpreting the `u64` as anything but identity.

**Focus Cursor**:
The single row a [[#List]]'s keyboard navigation acts on — the row arrow keys move, Home/End
jump to, type-ahead lands on, and Enter/Space commit. Named by [[#Item Id]] (stable id), so it
survives virtualization and re-sort like the selection does, and kept **distinct from the
selection**: a plain arrow move roves the cursor *without* changing the selection (the cursor
is "where I'm about to act", the selection is "what is chosen"), and distinct again from the
shift-extend **anchor**. It is live only while the List **holds keyboard focus** — a state the
List gains by a row click, a Tab-arrival, or an explicit **focus request** from the caller (a
freshly-appeared popup such as a dropdown asks the List to take focus and seeds the cursor
on the current value, since a popup has had neither a click nor a Tab). Granting focus is
separate from the application's global keyboard focus ring: a focus request both engages the
List's own keyboard handling *and* claims the global ring, so the previously-focused widget
(e.g. the dropdown trigger) blurs and the List keeps focus across frames.
_Avoid_: conflating the cursor with the selection (arrowing must not re-select in a
single-select list, or a dropdown would commit-and-close on every arrow press) or with the
anchor (the shift-range pivot stays put while the cursor roves). _Avoid_: setting the List's
focus flag without claiming the global ring — the next frame's global-focus check blurs it back.

**Drop Relationship**:
Where a [[#List]] drag-reorder lands, expressed relative to a target [[#Item Id]] rather than
as a flat insert index: `Before`, `After`, or `Into`. A flat list only ever produces
`Before`/`After` (sibling reorder); `Into` (reparent onto a node) is reserved for a future tree
view and never produced today.
_Avoid_: a bare `insert_before_index`/`insert_before_id` drop shape — it cannot express
reparenting.

