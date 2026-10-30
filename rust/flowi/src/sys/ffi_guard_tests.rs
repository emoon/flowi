//! ffi_guard round-trip: a panic inside a guarded C→Rust entry is logged,
//! poisons the plugin, and returns the slot's error value; once poisoned, later
//! entries early-return without running their body.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;

use super::{clear_poison, ffi_guard, is_poisoned, set_panic_logger};

/// Serialises the tests: poison and the logger are process-global.
static SERIAL: Mutex<()> = Mutex::new(());

static LOGGED: Mutex<Vec<String>> = Mutex::new(Vec::new());

fn record(message: &str) {
    LOGGED.lock().unwrap().push(message.to_owned());
}

#[test]
fn panic_is_logged_poisons_and_returns_error() {
    let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    clear_poison();
    set_panic_logger(record);

    assert_eq!(ffi_guard(-1, || 42), 42);
    assert!(!is_poisoned());

    let result = ffi_guard(-1, || -> i32 { panic!("boom in slot") });
    assert_eq!(result, -1);
    assert!(is_poisoned());
    assert!(
        LOGGED
            .lock()
            .unwrap()
            .iter()
            .any(|m| m.contains("boom in slot")),
        "the panic payload reached the log sink: {:?}",
        LOGGED.lock().unwrap()
    );

    let ran = AtomicBool::new(false);
    let result = ffi_guard(-2, || {
        ran.store(true, Ordering::SeqCst);
        99
    });
    assert_eq!(result, -2);
    assert!(
        !ran.load(Ordering::SeqCst),
        "poisoned guard must not run the body"
    );

    clear_poison();
    set_panic_logger(record);
    let owned = String::from("owned payload boom");
    let result = ffi_guard(-3, move || -> i32 { panic!("{}", owned) });
    assert_eq!(result, -3);
    assert!(
        LOGGED
            .lock()
            .unwrap()
            .iter()
            .any(|m| m.contains("owned payload boom")),
        "the String panic payload reached the log sink"
    );

    clear_poison();
    set_panic_logger(record);
    let result = ffi_guard(-4, || -> i32 { std::panic::panic_any(42u32) });
    assert_eq!(result, -4);
    assert!(
        LOGGED
            .lock()
            .unwrap()
            .iter()
            .any(|m| m == "<non-string panic payload>"),
        "a non-string payload logs the fallback message"
    );

    clear_poison();
}

#[test]
fn clear_poison_drops_the_installed_logger() {
    let _serial = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    clear_poison();
    set_panic_logger(record_cleared);
    clear_poison();

    let result = ffi_guard(-5, || -> i32 { panic!("after clear") });
    assert_eq!(result, -5);
    assert!(
        !CLEARED_LOGGED.load(Ordering::SeqCst),
        "a logger installed before clear_poison must not be called"
    );

    clear_poison();
    set_panic_logger(record_cleared);
    let result = ffi_guard(-6, || -> i32 { panic!("after reinstall") });
    assert_eq!(result, -6);
    assert!(
        CLEARED_LOGGED.load(Ordering::SeqCst),
        "a logger installed after clear_poison receives the next panic"
    );

    clear_poison();
}

static CLEARED_LOGGED: AtomicBool = AtomicBool::new(false);

fn record_cleared(_message: &str) {
    CLEARED_LOGGED.store(true, Ordering::SeqCst);
}
