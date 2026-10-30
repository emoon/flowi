**What this changes**

**Why**

**How it was verified**
<!-- Which of these you ran, and anything you could not test. -->
- [ ] `./build/unit_tests`
- [ ] `./build/gpu_smoke_test`
- [ ] `cargo test --manifest-path rust/Cargo.toml`
- [ ] `cargo regen-check` passes (only if a `.def` changed)

<!--
Checklist, per CONTRIBUTING.md:
- New tests are cases in unit_tests / gpu_smoke_test, not a new executable.
- No hand edits under */generated/*, include/flowi/ or src/exports/.
- Comments say what the code does; rationale goes in docs/adr/. No issue numbers.
- Sources are formatted (tools/scripts/format.sh staged, or the .githooks pre-commit hook).
-->
