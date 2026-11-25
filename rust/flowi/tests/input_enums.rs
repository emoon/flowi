//! The values of flowi's input enums that a host decoding OS events relies on: key blocks a host maps
//! by offset, the modifier bits it ORs together.

use flowi::{Key, KeyModifier};

#[test]
fn letter_digit_and_function_keys_are_contiguous_blocks() {
    for (offset, key) in [(1, Key::B), (2, Key::C), (25, Key::Z)] {
        assert_eq!(key as u32, Key::A as u32 + offset, "letter {key:?}");
    }
    for (offset, key) in [(1, Key::Num1), (2, Key::Num2), (9, Key::Num9)] {
        assert_eq!(key as u32, Key::Num0 as u32 + offset, "digit {key:?}");
    }
    for (offset, key) in [(1, Key::F2), (2, Key::F3), (11, Key::F12)] {
        assert_eq!(key as u32, Key::F1 as u32 + offset, "function key {key:?}");
    }
}

#[test]
fn modifier_flags_are_one_bit_each_and_combine() {
    assert_eq!(KeyModifier::SHIFT.bits(), 1 << 0);
    assert_eq!(KeyModifier::CONTROL.bits(), 1 << 1);
    assert_eq!(KeyModifier::ALT.bits(), 1 << 2);
    assert_eq!(KeyModifier::SUPER.bits(), 1 << 3);

    let combined = KeyModifier::SHIFT | KeyModifier::CONTROL;
    assert!(combined.contains(KeyModifier::SHIFT));
    assert!(combined.contains(KeyModifier::CONTROL));
    assert!(!combined.contains(KeyModifier::ALT));
}
