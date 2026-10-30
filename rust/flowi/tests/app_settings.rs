//! The settings-struct constructor brings a headless application up and drives a frame.

#![forbid(unsafe_code)]

use flowi::{AppSettings, Application, Renderer};

#[test]
fn with_settings_carries_every_field_headlessly() {
    let settings = AppSettings::new(64, 48)
        .with_title("settings")
        .headless(true)
        .with_renderer(Renderer::Software)
        .resizable(false);
    let mut app = Application::with_settings(settings);
    app.tick(&mut (), |_| {});
}
