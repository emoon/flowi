//! A caller-supplied label runs user code while `TextInput::show` prepares its arguments; that
//! code may itself call `Theme::update`, so the frame-thread lock must not be held yet.

use std::sync::mpsc;
use std::time::Duration;

use flowi::{col, grow, Application, TextInput, TextInputConfig, Theme, WidgetConfig};

/// Hang guard only: the assertion is that the frame completes, not how fast.
const HANG_GUARD: Duration = Duration::from_secs(30);

struct UpdatingLabel;

impl AsRef<str> for UpdatingLabel {
    fn as_ref(&self) -> &str {
        Theme::update(&Theme::colors());
        "label"
    }
}

fn frame(inputs: &mut [TextInput; 2]) {
    let [plain, configured] = inputs;
    col! { width: grow!(), height: grow!() {
        plain.show_default(UpdatingLabel);
        configured.show(
            UpdatingLabel,
            TextInputConfig {
                widget: WidgetConfig {
                    id_override: "configured_field",
                    ..Default::default()
                },
                ..Default::default()
            },
        );
    }}
}

#[test]
fn a_label_that_updates_the_theme_does_not_deadlock_show() {
    let (done, finished) = mpsc::channel();
    std::thread::spawn(move || {
        let mut app = Application::new(200, 150, true);
        let mut inputs = [TextInput::new(), TextInput::new()];
        assert!(app.tick(&mut inputs, frame));
        let _ = done.send(());
    });
    finished
        .recv_timeout(HANG_GUARD)
        .expect("TextInput::show hung while its label ran user code");
}
