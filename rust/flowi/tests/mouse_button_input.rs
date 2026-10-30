//! The mouse_button(id) query is global and unrouted - no Scroll Target, no widget Signal.

use flowi::{mouse_button, Application, MouseButton};

mod common;

// Any point inside the window; the button read is global.
const CURSOR_X: i32 = 100;
const CURSOR_Y: i32 = 60;

#[derive(Default)]
struct ButtonProbe {
    phase: Phase,
    activity_while_idle: bool,
    pressed_seen: bool,
    held_seen: bool,
    released_seen: bool,
    last_held: bool,
}

#[derive(Default, PartialEq)]
enum Phase {
    #[default]
    Idle,
    Down,
    Up,
}

// No layout is emitted: the query is global, so it needs no element and no render command.
fn frame(p: &mut ButtonProbe) {
    let s = mouse_button(MouseButton::Left);
    p.last_held = s.held;
    match p.phase {
        Phase::Idle => p.activity_while_idle |= s.pressed || s.held || s.released,
        Phase::Down => {
            p.pressed_seen |= s.pressed;
            p.held_seen |= s.held;
        }
        Phase::Up => p.released_seen |= s.released,
    }
}

#[test]
fn headless_mouse_button_state() {
    let mut app = Application::new(200, 120, true);
    let mut probe = ButtonProbe::default();

    app.inject_mouse_move(CURSOR_X, CURSOR_Y);
    common::tick_n(&mut app, &mut probe, frame, 4, "settle");
    assert!(
        !probe.activity_while_idle,
        "no button channel should report before any button event is injected"
    );

    // The event is drained by the pump and the press edge is captured for that frame; observe a couple of
    // frames so the edge (one frame) and the held level (sticky) both register.
    probe.phase = Phase::Down;
    app.inject_mouse_button(MouseButton::Left, true);
    common::tick_n(&mut app, &mut probe, frame, 3, "down");
    assert!(
        probe.pressed_seen,
        "mouse_button(Left).pressed should fire on the frame the press is pumped"
    );
    assert!(
        probe.held_seen,
        "mouse_button(Left).held should stay set while the button is down"
    );

    probe.phase = Phase::Up;
    app.inject_mouse_button(MouseButton::Left, false);
    common::tick_n(&mut app, &mut probe, frame, 3, "up");
    assert!(
        probe.released_seen,
        "mouse_button(Left).released should fire on the frame the release is pumped"
    );
    assert!(
        !probe.last_held,
        "mouse_button(Left).held should be clear once the button is released"
    );
}
