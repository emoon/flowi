//! The key queries are global and unrouted - no widget, no focus. Enter is probed alongside Delete as the
//! negative control.

use flowi::{Application, Key, UiInput};

mod common;

#[derive(Default)]
struct KeyProbe {
    phase: Phase,
    activity_while_idle: bool,
    pressed_frames: u32,
    down_seen: bool,
    released_frames: u32,
    down_on_release_frame: bool,
    last_down: bool,
    other_key_activity: bool,
}

#[derive(Default, PartialEq)]
enum Phase {
    #[default]
    Idle,
    Down,
    Up,
}

// No layout is emitted: the query is global, so it needs no element and no render command.
fn frame(p: &mut KeyProbe) {
    let pressed = UiInput::key_pressed(Key::Delete);
    let down = UiInput::key_down(Key::Delete);
    let released = UiInput::key_released(Key::Delete);
    p.last_down = down;
    p.other_key_activity |= UiInput::key_pressed(Key::Enter)
        || UiInput::key_down(Key::Enter)
        || UiInput::key_released(Key::Enter);
    match p.phase {
        Phase::Idle => p.activity_while_idle |= pressed || down || released,
        Phase::Down => {
            if pressed {
                p.pressed_frames += 1;
            }
            p.down_seen |= down;
        }
        Phase::Up => {
            if released {
                p.released_frames += 1;
                p.down_on_release_frame |= down;
            }
        }
    }
}

#[test]
fn headless_key_state() {
    let mut app = Application::new(200, 120, true);
    let mut probe = KeyProbe::default();

    common::tick_n(&mut app, &mut probe, frame, 4, "settle");
    assert!(
        !probe.activity_while_idle,
        "no key channel should report before any key event is injected"
    );

    // The event is drained by the pump and the press edge is captured for that frame; observe a couple of frames
    // so the edge (one frame) and the down level (sticky) both register.
    probe.phase = Phase::Down;
    app.inject_key(Key::Delete, true);
    common::tick_n(&mut app, &mut probe, frame, 3, "down");
    assert_eq!(probe.pressed_frames, 1, "Key::pressed(Delete) is a one-shot edge: it must fire on exactly the frame the press is pumped, not on every held frame");
    assert!(
        probe.down_seen,
        "Key::down(Delete) should stay set while the key is held"
    );

    probe.phase = Phase::Up;
    app.inject_key(Key::Delete, false);
    common::tick_n(&mut app, &mut probe, frame, 3, "up");
    assert_eq!(probe.released_frames, 1, "Key::released(Delete) is a one-shot edge: it must fire on exactly the frame the release is pumped");
    assert!(
        !probe.down_on_release_frame,
        "Key::down(Delete) must already be clear on the same frame the release edge fires"
    );
    assert!(
        !probe.last_down,
        "Key::down(Delete) should be clear once the key is released"
    );

    assert!(
        !probe.other_key_activity,
        "pressing Delete must not light up a different key (Enter)"
    );
}
