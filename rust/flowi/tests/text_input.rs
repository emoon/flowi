//! TextInput's mutators need no live Application and no frame.

use flowi::TextInput;

#[test]
fn set_get_clear_round_trip() {
    let mut input = TextInput::new();

    assert_eq!(input.text(), "");

    input.set_text("hello");
    assert_eq!(input.text(), "hello");

    // Overwriting replaces (not appends).
    input.set_text("world!");
    assert_eq!(input.text(), "world!");

    input.clear();
    assert_eq!(input.text(), "");

    // The handle is reusable after clear.
    input.set_text("again");
    assert_eq!(input.text(), "again");
}

#[test]
fn longer_than_default_capacity_round_trips() {
    // A string past the built-in floor exercises the on-demand buffer growth in set_text.
    let mut input = TextInput::with_capacity(8);
    let long = "x".repeat(512);
    input.set_text(&long);
    assert_eq!(input.text(), long);
}
