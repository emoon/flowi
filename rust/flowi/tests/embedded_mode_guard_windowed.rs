//! The mirror of embedded_mode_guard.rs. begin_frame, end_frame and push_input share the one
//! Application::host() accessor, so asserting the guard on begin_frame proves it for all three.

#![forbid(unsafe_code)]

use flowi::Application;

#[test]
#[should_panic(expected = "owns its window and frame loop")]
fn window_owning_application_rejects_the_embedded_frame() {
    let mut app = Application::new(32, 32, true);
    app.begin_frame(1.0 / 60.0);
}
