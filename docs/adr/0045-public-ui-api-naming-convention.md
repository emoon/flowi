# Public UI API naming: no `_ui_` infix, `_impl` is a generated symbol only, uniform config-struct calls

The public C function name is mechanical: `fl_` + the `.def` `api` block name (snake_cased)
+ the function name (ADR-0030, ADR-0031). So every inconsistency on the public UI surface is
a `.def` authoring choice, not a generator bug — and the surface had three:

- **`_ui_` infix by coin-flip.** `api UiMenu`/`UiInput`/`UiLayout`/`UiList`/`UiScroll` produced
  `fl_ui_menu_*`, `fl_ui_input_*`, … while `api Button`/`Slider`/`Painter`/`Cursor`/`Popup`
  produced `fl_button_*`, `fl_slider_*`, …. No rule decided which modules got `Ui`.
- **`_impl` leaking onto the public surface.** Functions were named `radio_impl`, `begin_impl`
  in the `.def`, so the public symbol was `fl_ui_menu_radio_impl`. The wrapper macro that
  supplies the trailing config struct via designated initializers was `FL_UI_MENU_RADIO_IMPL`
  (SHOUTY). Because C identifiers are case-sensitive, a lowercase macro does not collide with a
  lowercase function — so the `_impl` token on the *public* surface was gratuitous, inherited
  from the internal `ui_*` convention where a lowercase macro (`ui_button`) genuinely needs a
  differently-named callee (`ui_button_impl`).
- **Config-struct vs all-explicit args, inconsistently.** `fl_ui_menu_radio(label, sel, val,
  config)` took a trailing config struct (hence a macro, hence `_impl`); `fl_slider_float_value(
  label, value, min, max, format, step, logarithmic, vertical)` spelled every option as a
  positional arg, took no macro, and carried a `_value` suffix that only named "the
  pointer-binding variant".

This ADR sets the standing naming policy for the generated public `fl_*` UI surface. It refines
the prefix policy of ADR-0031, sits on top of the forwarder-body pattern of ADR-0032, and is
orthogonal to the Rust façade (ADR-0027) and the doc-comment policy (ADR-0044).

## Decision

**1. No `_ui_` infix.** UI modules are named for the widget/domain only: `api Menu` → `fl_menu_*`,
`api Input` → `fl_input_*`, `api Layout` → `fl_layout_*`, `api Checkbox` → `fl_checkbox_*`. The
`fl_` prefix already names the library; the module name already disambiguates within it. Core
services keep their own self-evident module names (`fl_vfs_*`, `fl_job_*`, `fl_log_*`, `fl_json_*`)
— `fl_` vs `fl_ui_` was never the thing distinguishing them.

**2. Call sites are lowercase and function-like; `_impl` is a generated symbol only.** A widget
whose trailing parameter is a `*Config` struct is *called* by its plain lowercase name (`fl_menu_radio(label, &sel, val,
.shortcut = …)`). That name is a function-like macro forwarding to the real function, whose symbol
carries `_impl` (`fl_menu_radio_impl`). `_impl` therefore appears only in the generated header as
the callee — a human never types it. Macros are lowercase, never SHOUTY. This makes the public
surface match the internal `ui_*` convention. The macro exists only for a trailing `*Config`
struct: a slot with no config parameter has no `_impl` and no macro (`fl_menu_end`, `fl_spacer`),
and a trailing `*Opts`/`*Options` struct is passed explicitly to a plain function (`fl_image`,
`fl_dynamic_image`, `fl_list_uniform_begin`).

**3. Uniform shape: required positional args + one trailing config struct.** A widget's arguments
split by necessity: *positional* = cannot use the widget without it (the label, the bound pointer,
a slider's `min`/`max`); *config struct* = has a sensible default (`format`, `step`, `vertical`,
colors, id overrides). Defaults are the config struct's zero values, filled in by the wrapper
macro's designated initializer (`(FlWidgetConfig) { __VA_ARGS__ }`). All-explicit
argument lists are not used, and a suffix whose only job was to name the binding variant (the
`_value` in `fl_slider_float_value`) is removed — the bound variant is simply *the* widget
(`fl_slider_float`).

**4. Suffixes name real variants only.** A trailing word after the module name must distinguish a
genuine sibling, never filler. `fl_slider_float` / `fl_slider_int` pass (the type is the variant);
`fl_button_label` / `fl_button_image` pass (image-or-not is the variant); `fl_button_regular` does
not. The generator is **not** special-cased to emit a bare `fl_button`; a module's calls always
carry a real, distinguishing function name. **(Amended 2026-06-26 — see "Amendment" below: a block
may now designate one *primary* slot that emits the bare `fl_<block>`; the anti-filler rule survives
for every non-primary slot.)**

**5. Scope.** This governs the generated public `fl_*` UI surface only. The internal `ui_*` layer
(`flowi/src/ui/`) is already self-consistent under the same lowercase-macro/`_impl` convention and
is left unchanged. The Rust façade is unaffected: it already drops the `fl_`/`Fl` prefix (ADR-0027)
and strips a trailing `_impl`, so `fl_menu_radio_impl` is `Menu::radio` either way.

## Considered Options

- **UPPERCASE macros (`FL_MENU_RADIO`) to delete the `_impl` token entirely.** This is the only way
  to physically remove `_impl` from the headers, because then the function can own the bare lowercase
  name. Rejected: it forces SHOUTY call sites on the most-typed lines in any immediate-mode app. The
  token surviving as an untyped generated symbol is the cheaper cost.
- **Same-name macro and function via "blue paint" (no `_impl`, no SHOUTY).** Rejected: a function-like
  macro sharing the function's name mangles the prototype line and demands strict declaration ordering
  — too fragile for generated headers included in arbitrary order.
- **Apply `_ui_` to every UI call as a deliberate sub-namespace.** Rejected: redundant with the module
  name, and it adds three characters to the calls users type every frame for no disambiguation the
  module name does not already provide.
- **Keep all-explicit argument lists (no config struct).** Rejected: no defaults, brittle call sites
  that break on every new option, and it is the very split that produced the `fl_slider_*_value`
  outlier.
- **Special-case the generator to emit bare module names (`fl_button`).** Rejected: a generator
  special case to save a suffix that should be a real variant word anyway; mandating real suffixes
  (decision 4) is simpler and removes only filler.
- **Deprecation aliases instead of a big-bang rename.** Rejected for execution: the surface is fully
  generated with all call sites in-tree, so aliases would just be a second inconsistent naming surface
  plus a cleanup that never lands. The rename is done in one regen + mechanical caller sweep.

## Consequences

- The entire public C UI surface is renamed in one regeneration; in-tree callers, `unit_tests`, and
  `gpu_smoke_test` are swept in the same change. The Rust façade names barely move.
- The same pass exposes the internal-only widgets (checkbox, switch, radio_group, progress_bar,
  slider_float/int, pill, chip, dropdown, tab_bar, link, scrollbar, button_label/image, text_styled,
  image, dynamic_image, spacer) — each is one `.def` entry plus one forwarder in `ui_api.c`
  (ADR-0032); their config structs are already public.
- `ui_core_display_impl` is excluded — core output belongs to the host display plane, not a widget,
  so the deprecated stub is retired rather than entering flowi's public surface.
- Future `.def` authoring follows decisions 1–4; a reviewer has a concrete rule to point at instead of
  re-litigating each widget.

## Amendment (2026-06-26): primary slots emit bare names

Decision 4 forbade a bare `fl_button`. In practice its three worked examples are all *multi-variant*
widgets, and the rejected `fl_button_regular` is *filler hiding a real variant* — so the rule it was
actually enforcing is "no filler suffixes," not "every call needs a suffix." A single-form widget
(checkbox, switch, link, …) has no variant to name, and forcing one (`fl_checkbox_show`,
`fl_toggle_checkbox`) reintroduces exactly the filler decision 4 set out to remove. This amendment
restores the bare name as the *correct* spelling of a primary form, in both C and Rust.

**Rule.** An `api` block MAY designate **one primary slot**, spelled `new` (or `new_impl` when it
takes a trailing config struct) in the `.def`:

- The primary slot emits the **bare** `fl_<block>` in C — for the config-taking case, body
  `fl_<block>_impl` plus the lowercase convenience macro `fl_<block>` (the decision-2 mechanism, with
  the slot word dropped) — and **`<Block>::new(...)`** in the Rust façade.
- **Non-primary slots are unchanged:** `fl_<block>_<variant>` / `<Block>::<variant>`, and decision 4's
  anti-filler rule still governs them in full (`fl_button_image` / `Button::image`).
- A block with **no natural primary** (Slider — the type *is* the variant) designates none; all slots
  stay suffixed. Marking a primary is opt-in.
- Retrofit: `api Button`'s `label` slot becomes the primary (`fl_button` / `Button::new`); `image`
  stays `fl_button_image` / `Button::image`.

**Mechanism.** Spelling the primary slot `new` makes the marker carry its own meaning — it is
literally the Rust method name — so no attribute is wasted and no authored name is silently dropped.
The generator special-cases `new` / `new_impl` exactly as it already special-cases the `_impl`
suffix: for that slot the C name omits the slot word (`fl_<block>` / `fl_<block>_impl`) and the Rust
method is `new`.

**Trade-off accepted.** `<Block>::new(...)` returns `Signal`, not `Self`, so `new` bends the Rust
constructor convention. Rejected alternatives: a free function `flowi::checkbox(...)` (splits primary
from variants across module root vs. struct, hurting discovery) and keeping a descriptive method
while only C goes terse (diverges the C and Rust names for the primary). `new` was chosen for
cross-language symmetry of intent — the primary needs no qualifier in either language — over strict
constructor honesty, which immediate-mode façades break everywhere regardless.

This was delivered as its own change (api_gen `new`-slot support + the `fl_button` / `Button::new`
retrofit) ahead of the internal-widget exposure, so the widgets landed under the settled rule.

## Amendment (2026-10): current generator behaviour

The generated headers now follow these rules, which refine decisions 2 and 3 and the "blue paint"
alternative above.

- **The convenience macro is generated only for a trailing `*Config` struct.** `fl_button`,
  `fl_checkbox`, `fl_menu_item` and the rest are `fl_<name>_impl(…, (FlXConfig) { __VA_ARGS__ })`.
  A trailing `*Opts`/`*Options` struct is an ordinary positional argument, so `fl_image` and
  `fl_dynamic_image` are plain functions with no `_impl` and no macro; a config-less slot
  (`fl_menu_end`, `fl_menu_separator`, `fl_popup_end`) is a plain function with no
  `_impl`.
- **Slots authored without the `_impl` suffix but taking a trailing `*Config`** (`Text::styled`,
  `Painter::rect_styled`, `Popup::begin`, `Layout::configure`) are emitted the other way round:
  the function keeps the bare name and a same-name macro supplies the config
  (`#define fl_text_styled(text, ...) fl_text_styled(text, (FlTextConfig) { __VA_ARGS__ })`, guarded
  so the implementation file sees the function), plus an `fl_<name>_impl` alias macro. This is
  the same-name macro-and-function shape the "blue paint" alternative above rejected, so the
  rejection no longer holds for these slots; the `_impl`-suffixed form is the one the widgets use.
