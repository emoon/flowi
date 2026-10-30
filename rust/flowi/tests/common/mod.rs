//! Input-driving helpers shared by the headless integration tests.

#![allow(dead_code)]

use flowi::{Application, ElementState, MouseButton, Vec2};

/// Runs `n` ticks of `frame`, asserting each one keeps the application running.
pub fn tick_n<T>(
    app: &mut Application,
    state: &mut T,
    mut frame: impl FnMut(&mut T),
    n: usize,
    what: &str,
) {
    for _ in 0..n {
        assert!(
            app.tick(state, &mut frame),
            "{what} tick should keep running"
        );
    }
}

/// Moves the cursor to `(x, y)`, holds it for two ticks so hot state settles, then presses and releases
/// `button`, ticking once per edge.
pub fn press_release_at<T>(
    app: &mut Application,
    state: &mut T,
    frame: fn(&mut T),
    button: MouseButton,
    x: i32,
    y: i32,
) {
    app.inject_mouse_move(x, y);
    assert!(app.tick(state, frame), "hover tick 1");
    assert!(app.tick(state, frame), "hover tick 2");
    app.inject_mouse_button(button, true);
    assert!(app.tick(state, frame), "{button:?} press");
    app.inject_mouse_button(button, false);
    assert!(app.tick(state, frame), "{button:?} release");
}

/// Left-clicks at `(x, y)` in window pixels.
pub fn click_at<T>(app: &mut Application, state: &mut T, frame: fn(&mut T), x: i32, y: i32) {
    press_release_at(app, state, frame, MouseButton::Left, x, y);
}

/// Right-clicks at `(x, y)` in window pixels.
pub fn right_click_at<T>(app: &mut Application, state: &mut T, frame: fn(&mut T), x: i32, y: i32) {
    press_release_at(app, state, frame, MouseButton::Right, x, y);
}

/// Centre of `e`'s rect, truncated to whole window pixels.
pub fn center(e: ElementState) -> (i32, i32) {
    let Vec2 { x, y } = e.pos;
    ((x + e.size.x * 0.5) as i32, (y + e.size.y * 0.5) as i32)
}
