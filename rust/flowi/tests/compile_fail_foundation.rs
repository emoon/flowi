//! Compile-fail proofs for the typed arena's ownership guarantees: each `tests/ui/*.rs`
//! must fail to compile with the committed .stderr.

#[test]
fn arena_ownership_guarantees_are_enforced() {
    let t = trybuild::TestCases::new();
    t.compile_fail("tests/ui/use_after_rewind.rs");
    t.compile_fail("tests/ui/collection_across_rewind.rs");
    t.compile_fail("tests/ui/temp_scope_escape.rs");
    t.compile_fail("tests/ui/frame_scratch_escape.rs");
    t.compile_fail("tests/ui/base_arena_rewind.rs");
    t.compile_fail("tests/ui/shutdown_needs_unsafe.rs");
}
