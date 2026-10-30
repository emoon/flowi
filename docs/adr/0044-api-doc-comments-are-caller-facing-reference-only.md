# API doc comments are caller-facing reference only; api_gen rejects rationale refs

`.def` `///` comments are copied verbatim into the generated public surface — the C
headers (`c_gen.rs`) and the Rust rustdoc (`rust_gen.rs`), both in the api_gen repository
(ADR-0073). Over time they accumulated
content written for a *maintainer reading the IDL*, not a *caller reading the API*:
forwarder/impl-body mechanics ("thin forwarders to the real `ui_list_*` free functions"),
design rationale, authorship notes ("flowi authors this type"), tracker/ADR cross-refs
(an issue number, `ADR-0032`), and `(*.def)`/`(*.h)` source-file pointers. The same stock phrase
("…exposed to Rust through the generated direct API (ADR-0032)") was duplicated
across `app.def`, `item_behavior.def`, `ui_list.def`, and more. The result is
public documentation a consumer cannot read. This extends ADR-0030 (the `.def` is the
single source of truth) with a content policy for the doc comments it carries.

## Decision

**A `///` doc comment in a `.def` is caller-facing reference only — written for someone
consuming the API who never sees the IDL.** Concretely:

- **Content.** What the function does, its parameters, its return, and how it pairs with
  its siblings. Type names are named plainly (`Signal`, `ListResult`); `(*.def)`/`(*.h)`
  source-file pointers are not — the `.def` layout is an internal implementation detail.
- **Altitude.** The `api`-block comment is a one-or-two-line summary of what the scope *is*
  and how its calls are used together (e.g. the `begin → item → end` pairing). It never
  restates individual function signatures. Each function comment documents only that
  function.
- **No motivation in any code comment.** Rationale, design notes, and "why it's built this
  way" do not belong in `///` *or* `//` — they live in an ADR (a decision) or CONTEXT.md (a
  term). Nothing is demoted to `//` to "keep it nearby"; if it is worth keeping it goes to
  an ADR.
- **api_gen enforces the mechanical floor.** The generator fails with an error (naming file,
  line, and pattern) if a `///` line contains an issue ref (`#` + digits), a decision ref
  (`ADR`), a `(*.def)`/`(*.h)` file pointer, or the fixed generator-meta phrase `exposed to
  Rust through the generated direct API`. The same check also runs over plain `//` comments in
  the `.def`. These never legitimately appear in a public doc comment, so the gate has no false
  positives. The prose-quality rule ("write for the
  consumer") is not lintable and stays a review concern.

## Considered Options

- **Soft cleanup — trim the worst, allow some rationale to remain.** Rejected: a fuzzy rule
  does not survive the next agent editing a `.def`; the bloat was systemic precisely because
  there was no hard line.
- **Demote rationale to `//` comments in the `.def`.** Rejected: it relocates clutter rather
  than removing it, and creates a third copy of what already lives in the ADRs/CONTEXT.md,
  free to drift.
- **A grep guard in the generation script (`gen_bindings.sh`, since replaced by `cargo regen`)
  instead of in api_gen.** Reasonable and cheaper, but
  api_gen is the stronger seam — it runs before the build, owns the parse, and turns a
  violation into a generation error at the authoring gate. Chosen over the external grep.
- **Also ban boilerplate *phrases* ("thin forwarder", "hand-written body").** Rejected for
  the general case: substring-matching prose invites false positives and an arms race; the
  sweep deletes the phrases anyway, and review keeps them out. The **one** exception is
  `exposed to Rust through the generated direct API` — a fixed generator-meta phrase with no
  plausible legitimate use, hit repeatedly across files, so it joins the gate as a token-like
  exact match.

## Consequences

- All `api/*.def` files are swept to the new standard in one pass.
- Reintroducing an issue/ADR/file-pointer ref in a `///` is now a hard generation error, not
  a review nit that can slip through.
- The mechanical gate does not catch rationale *prose* with no banned token; that remains a
  review responsibility, backed by this ADR.
