//! Submission cost of a dense scope-style canvas through the Rust painter: `Painter::rect` once per rectangle
//! versus one `Painter::rects` call. Headless, no display needed; timing is only meaningful in release:
//!
//! ```text
//! cargo run --release -p flowi --example painter_rects_bench
//! ```

use flowi::{grow, layout, Application, Color, ColoredRect, Painter};
use std::time::{Duration, Instant};

const CHANNELS: usize = 4;
const WIDTH: usize = 1920;
const FRAMES: usize = 25;

fn channel(c: usize) -> Vec<ColoredRect> {
    (0..WIDTH)
        .map(|x| {
            let amp = 80.0 * (x as f32 * 0.05 + c as f32).sin();
            ColoredRect {
                x: x as f32,
                y: 120.0 + c as f32 * 240.0 - amp.max(0.0),
                w: 1.0,
                h: amp.abs() + 1.0,
                color: Color {
                    r: 60 + 50 * c as u8,
                    g: 220 - 40 * c as u8,
                    b: 120,
                    a: 180,
                },
            }
        })
        .collect()
}

/// Best-of-`FRAMES` time spent inside the painter calls of one frame.
fn best_submission(app: &mut Application, channels: &[Vec<ColoredRect>], batch: bool) -> Duration {
    let mut best = Duration::MAX;
    let mut state = ();
    for _ in 0..FRAMES {
        let mut spent = Duration::ZERO;
        app.tick(&mut state, |_: &mut ()| {
            layout! { width: grow!(), height: grow!() {
                let t0 = Instant::now();
                for rects in channels {
                    if batch {
                        Painter::rects(rects);
                    } else {
                        for r in rects {
                            Painter::rect(r.x, r.y, r.w, r.h, r.color);
                        }
                    }
                }
                spent = t0.elapsed();
            }}
        });
        best = best.min(spent);
    }
    best
}

fn main() {
    let mut app = Application::new(WIDTH as u32, 1080, true);
    let channels: Vec<_> = (0..CHANNELS).map(channel).collect();

    let scalar = best_submission(&mut app, &channels, false);
    let batched = best_submission(&mut app, &channels, true);
    println!(
        "{} rects: scalar loop {:?}, batched {:?} ({:.2}x)",
        CHANNELS * WIDTH,
        scalar,
        batched,
        scalar.as_secs_f64() / batched.as_secs_f64()
    );
}
