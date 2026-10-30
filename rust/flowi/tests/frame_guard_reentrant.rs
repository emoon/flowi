//! A caller's own `AsRef` / `Into` implementation runs before the frame guard is taken, so it may
//! itself call guarded entry points without deadlocking on the non-reentrant guard.

use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use flowi::{Application, Button, Cursor, Item, ItemFlags, LayoutId, Theme};

mod common;

static CONVERSIONS: AtomicUsize = AtomicUsize::new(0);

/// Calls a hand-guarded and a generated guarded entry point from inside `as_ref`.
struct ReentrantLabel;

impl AsRef<str> for ReentrantLabel {
    fn as_ref(&self) -> &str {
        let _ = Theme::colors();
        let _ = Cursor::get_visible();
        CONVERSIONS.fetch_add(1, Ordering::SeqCst);
        "reentrant"
    }
}

/// Calls a guarded entry point from inside `into`.
struct ReentrantId;

impl From<ReentrantId> for LayoutId {
    fn from(_: ReentrantId) -> LayoutId {
        let _ = Cursor::get_visible();
        CONVERSIONS.fetch_add(1, Ordering::SeqCst);
        "reentrant_id".into()
    }
}

fn frame(_: &mut ()) {
    Button::new(ReentrantLabel, Default::default());
    Item::behavior(ReentrantId, ItemFlags::CLICKABLE);
}

#[test]
fn user_conversions_may_call_guarded_entry_points() {
    let finished = Arc::new(AtomicBool::new(false));
    let watchdog = {
        let finished = Arc::clone(&finished);
        thread::spawn(move || {
            for _ in 0..300 {
                if finished.load(Ordering::SeqCst) {
                    return;
                }
                thread::sleep(Duration::from_millis(100));
            }
            eprintln!("a user conversion deadlocked on the frame guard");
            std::process::exit(1);
        })
    };

    let mut app = Application::new(200, 120, true);
    common::tick_n(&mut app, &mut (), frame, 2, "reentrant conversions");
    assert!(
        CONVERSIONS.load(Ordering::SeqCst) >= 4,
        "both conversions should have run on each frame"
    );

    finished.store(true, Ordering::SeqCst);
    watchdog.join().unwrap();
}
