//! The mirror of embedded_mode_guard.rs. tick_embedded and push_input share the one
//! Application::host() accessor, so asserting the guard on tick_embedded proves it for both.

#![forbid(unsafe_code)]

use flowi::Application;

#[test]
#[should_panic(expected = "owns its window and frame loop")]
fn window_owning_application_rejects_the_embedded_tick() {
    let mut app = Application::new(32, 32, true);
    app.tick_embedded(1.0 / 60.0, || {});
}
