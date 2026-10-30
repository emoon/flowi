//! Opens a real SDL window through run() and presents the clear color until the
//! window is closed. It needs a display; run it with:
//!
//! ```text
//! cargo run -p flowi --example window
//! ```

use flowi::Application;

fn main() {
    let app = Application::new(1280, 720, false);
    // run blocks until the window is closed.
    app.run(0u64, |frame: &mut u64| {
        *frame += 1;
    });
}
