# UI feedback is a semantic event seam; flowi reports what happened, the app owns the effect

## Status

accepted

## Context

With the audio backends evicted from flowi (the audio-backend removal), flowi-based apps still want
*optional* UI sound effects — a tick as focus moves between elements, a confirm sound on activation.
The tempting shape is a sound-effect backend inside flowi: a `play_navigate` / `play_confirm`
vocabulary the toolkit calls. That drags audio semantics right back into the library it was just
removed from — flowi would own a catalog of sounds and the policy of which fires when.

The interaction transitions these effects want to ride are already modeled centrally: spatial nav
(arrow/D-pad) and Tab-order moves resolve in `ui_update_end`, and keyboard/gamepad activation is
computed in `ui_item_behavior` (the `signal.activated` edge, guarded so a widget doesn't self-activate
on the frame it gains focus). The emit points fall out of the existing focus/nav layer; nothing new
needs to be tracked.

This is the same report-don't-own razor as the clipboard seam and the background split: the toolkit
reports an event at the right altitude and the app decides the effect. And it is not audio-specific —
the identical stream serves haptics/rumble (gamepad-first UI), screen-reader / accessibility
announcements, and interaction telemetry. Naming it "feedback" rather than "sound" keeps all of those
open.

## Decision

flowi emits **semantic UI feedback events** through one app-installed hook and stays entirely ignorant
of audio, haptics, and a11y. The seam is a hand-written public header (`flowi/include/flowi/ui/feedback.h`,
like `nav.h`), not api_gen output.

- **One hook, one kind enum.** `fl_ui_set_feedback_hook(cb, user_data)` installs (or clears, with
  `cb == NULL`) a single callback; the app filters by `FlUiFeedbackEvent.kind`. No per-kind subscription
  registry — one hook the app switches on is the simplest thing that covers every consumer.

- **Starter vocabulary: `NavMoved`, `NavBlocked`, `Activated`.** This is exactly what the existing
  nav/activation paths already know. `NavMoved` carries `from`/`to` widget ids and the `dir` pressed
  (spatial nav) or `None` (Tab order); `NavBlocked` fires when a direction hits an edge (no candidate),
  carrying `from`/`dir` with `to == 0`; `Activated` carries the activated widget in `to`. The enum is
  deliberately open — `Toggled`, `PopupOpened`/`Closed`, `Error`, text-input edges are future members that
  add to the stream without changing the seam, since apps switch on `kind` and ignore what they don't
  handle.

- **App-wide, on the shared tier.** The hook lives on `UiShared` (one hook serves every window), set via
  the current context in `fl_ui_set_feedback_hook`. Focus/activation feedback is an app-level concern, not
  a per-window one.

- **Zero cost when unused.** Events fire only on discrete, human-driven interactions — never per frame.
  The emit is a null-check on the stored hook plus a call with a borrowed stack-allocated
  `FlUiFeedbackEvent`. No allocation, no retained state. With no hook installed (the default) the paths are
  a single branch.

- **Ids are within-frame `FlElementId`.** `from`/`to` are the Clay hashed ids the focus system already
  keys on (what `fl_focused_id` returns), not stable cross-frame handles — consistent with the rest of the
  element-state surface (ADR-0023).

### Naming

The setter is spelled `fl_ui_set_feedback_hook` (the `_ui_` infix),
even though ADR-0045 strips `_ui_` from the UI surface. ADR-0045 governs the *generated* `fl_*` surface;
this is a hand-written seam, and the `ui` here disambiguates from the theme's `feedback` colors
(`FlThemeColors.feedback.*`) that already exist. The event types keep the `FlUiFeedback*` prefix for the
same reason.

## Considered alternatives

- **A sound-effect backend inside flowi (`play_navigate`, …).** Rejected: re-imports the exact audio
  ownership just evicted, and bakes in a fixed sound vocabulary plus the policy of which sound maps to
  which interaction — none of which is flowi's to decide.
- **Per-kind subscriptions (register a callback per event type).** Rejected: a registry and lifetime for
  every kind, to save one `switch` in the app. One hook + a kind enum is strictly simpler and the app
  filters trivially.
- **An audio-specific hook (`fl_ui_set_sound_hook`, a `SoundKind` enum).** Rejected: closes the door on
  haptics/a11y/telemetry that the identical event stream serves. "Feedback" is the general name; sound is
  one app-side interpretation.
- **Emit from every focus mutation (mouse focus, auto-restore, popup focus).** Rejected for the starter
  set: the seam is scoped to the *nav/activation* paths (keyboard/gamepad-driven discrete interactions),
  which is what UI-feedback effects actually key on. Mouse-driven and programmatic focus changes are out of
  scope and can be added as their own kinds if a consumer needs them.

## Consequences

- Downstream apps attach UI sound / haptics / a11y / telemetry by installing one hook and mapping
  `kind` → effect. The out-of-repo embedding host routes `NavMoved` to its own audio service; flowi
  never learns audio exists. `examples/widget_gallery` carries an in-repo proof-of-concept sink (a log
  line naming the sound it would play) since the monorepo has no audio subsystem.
- Event emission is assertable headlessly by recording the callback — the `feedback.*` cases in
  `unit_tests` drive nav/activation and assert the emitted kind/from/to/dir.
- Widening the vocabulary (new `FlUiFeedbackKind` / `FlUiFeedbackDir` members) is additive: apps ignore
  kinds they don't handle. This header is not plugin ABI (hand-written, not an IDL/data-layout type), so a
  new member is a normal source change, not a coordinated ABI rebuild.
- The emit points are the central nav/activation transitions; a future nav rework updates them in place
  rather than threading feedback through every widget.
