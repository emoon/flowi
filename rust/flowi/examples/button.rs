//! A col! / row! layout containing a Button::new("Click me"), reacting to its .clicked signal.
//! It needs a display; run it with:
//!
//! ```text
//! cargo run -p flowi --example button
//! ```

use flowi::{col, grow, row, Application, Button};

fn main() {
    let app = Application::new(800, 600, false);
    app.run(0u64, |clicks: &mut u64| {
        col! { width: grow!(), height: grow!() {
            row! { {
                if Button::new("Click me", Default::default()).clicked {
                    *clicks += 1;
                    println!("clicked {} time(s)", clicks);
                }
            } }
        }}
    });
}
