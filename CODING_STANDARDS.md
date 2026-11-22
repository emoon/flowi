# Coding standards

The rules a review checks a change against. `tools/scripts/check_style.sh` enforces the mechanical ones (`nullptr`, `sizeof_array` for `sizeof(a) / sizeof(a[0])`, separator lines, null checks on `g_ui_ctx`); the rules here need judgement. Naming, comments and formatting basics are in `docs/CONTRIBUTING.md`.

## Public API

- A new exported symbol has a caller in a host's product code. A caller in a test alone leaves the symbol internal.
- A public function is the implementation itself, carrying the `fl_` name. A function that only forwards to an internal one is the same function twice.
- A removed or changed line in `api/*.def` or `include/flowi/` names the hosts outside this repository that were checked. "Nothing in `src/` reads it" describes flowi, and the consumers are elsewhere.
- Examples and probes include public headers only.

## Contracts

- A precondition is checked once, where the object is created. Code after that point relies on it: no per-call null guards, no accessor that re-validates.
- An `FlString` is its length. Code walks `length` bytes and never looks for a terminator.
- A safe Rust wrapper upholds what the C function only asserts, because a Release build compiles the assert out.

## Size and shape

- Ask whether a much smaller change does the same job, and say so when it does. A new subsystem several times the size of its one-sentence description needs each extra part justified by a measurement.
- A struct holds state that several steps use. A struct that one caller fills and one callee unpacks is an argument list with more code.
- A change made for a Code Health score follows `docs/codescene.md` and keeps behaviour: the same guards, the same early returns, the same thread owning each field.
- A new file goes in its family's directory (`src/ui/list/`, `tests/vfs/`).

## Comments

- A comment says what the code does. Reasons, history and what a later change will do belong in an ADR or the pull request.
- A function that moves takes its leading comment with it.

## Loops

- A loop over `0..count` is `for_count(i, n)`, including a loop whose body used to step the index itself. A stepped range or a pointer walk stays a plain `for`.

## Rendering and layout

- A change under `src/render/software/` or to `src/core/simd.h` comes with old-against-new timing on full frames.
- A length is in logical or physical pixels, and arithmetic stays in one of them. Check layout code at a UI scale other than 1.0, where a mixed unit shows.

## Tests

- A test passes in Debug and in Release. Sizes and layouts that differ between them (`#ifndef NDEBUG` fields, asserts with side storage) are the usual cause when it does not.
- An assertion holds on any machine: no exact float comparison, no sleep-based timing, no address identity.
- A split test file keeps every test and every assertion; compare the counts before and after.
