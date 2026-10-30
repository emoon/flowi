//! A minimal wheel-input demo: a single full-window col! region declared a Scroll Target, reading its
//! per-frame Wheel Delta and accumulating it into an owned scroll offset it prints. It needs a display and
//! a mouse wheel; run it and scroll over the window:
//!
//! ```text
//! cargo run -p flowi --example wheel
//! ```

use flowi::{col, grow, Application, UiInput};

const REGION_ID: &str = "wheel_region";

fn main() {
    let app = Application::new(800, 600, false);
    // flowi reports per-frame deltas; the cumulative position is the app's.
    app.run(0.0f32, |scroll_y: &mut f32| {
        col! { REGION_ID, width: grow!(), height: grow!(), color: [40, 40, 60] {
            UiInput::scroll_target(REGION_ID);
            let d = UiInput::mouse_wheel_delta(REGION_ID);
            if d.y != 0.0 {
                *scroll_y += d.y;
                println!("wheel {:+.0} -> scroll_y {:.0}", d.y, scroll_y);
            }
        }}
    });
}
