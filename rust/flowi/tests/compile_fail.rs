//! Compile-fail proofs for the pointer-safety surface: each `tests/ui/*.rs` must fail to compile with the
//! committed .stderr.

#[test]
fn list_result_and_dynamic_image_cannot_be_misused() {
    let t = trybuild::TestCases::new();
    t.compile_fail("tests/ui/list_result_not_copy.rs");
    t.compile_fail("tests/ui/list_result_private_field.rs");
    t.compile_fail("tests/ui/list_result_forged.rs");
    t.compile_fail("tests/ui/dynamic_image_new_needs_unsafe.rs");
    t.compile_fail("tests/ui/window_closed_twice.rs");
}
