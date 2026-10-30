//! The whole window-owning set (run, open_window, the `inject_*` harness) shares the one
//! Application::app() accessor, so asserting the guard on tick proves it for all of them.

#![forbid(unsafe_code)]

use flowi::{Application, EmbeddedSettings};

#[test]
#[should_panic(expected = "created with `Application::embedded`")]
fn embedded_application_rejects_the_window_owning_tick() {
    let mut app = Application::embedded(EmbeddedSettings::new(32, 32)).expect("embedded flowi");
    app.tick(&mut (), |_: &mut ()| {});
}
