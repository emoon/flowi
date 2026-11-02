//! Painter::rects against the scalar Painter::rect loop, on the software backend.
//!
//! Two canvases: a clipped, translucent, overlapping scope-style run of columns, and a later floating box that
//! must cover part of the first one (Canvas z-order). The scene is drawn once with a rect loop and once with a
//! single rects call; the readbacks must match exactly, at unit scale and at a fractional UI zoom. One test,
//! because the toolkit drives a single global frame context per process.

use flowi::{col, fixed, floating, grow, layout, Application, Color, ColoredRect, Painter};

const CLEAR: [u8; 3] = [14, 14, 16];

fn scope_columns(count: usize, seed: f32) -> Vec<ColoredRect> {
    (0..count)
        .map(|i| ColoredRect {
            x: seed + i as f32 * 0.75,
            y: 2.0 + (i % 7) as f32 * 0.5,
            w: 1.5,
            h: 10.0 + (i % 5) as f32 * 3.0,
            color: Color {
                r: 40 + (i as u8 % 9) * 20,
                g: 200 - (i as u8 % 11) * 10,
                b: 90,
                a: 90 + (i as u8 % 6) * 30,
            },
        })
        .collect()
}

fn draw_scene(batch: bool, a: &[ColoredRect], b: &[ColoredRect]) {
    let submit = |rects: &[ColoredRect]| {
        if batch {
            Painter::rects(rects);
        } else {
            for r in rects {
                Painter::rect(r.x, r.y, r.w, r.h, r.color);
            }
        }
    };
    layout! { width: grow!(), height: grow!() {
        // 40x24 canvas whose columns run past its left and bottom edges: the element clip must cut them.
        col! { "scope_a", width: fixed!(40.0), height: fixed!(24.0) {
            submit(a);
        }}
        col! { "scope_b", width: fixed!(30.0), height: fixed!(20.0) {
            submit(b);
        }}
        // Declared later, so it composes over scope_a where they overlap.
        floating! { "cover", width: fixed!(10.0), height: fixed!(10.0), offset: [12.0, 6.0], z_index: 1 {
            col! { width: grow!(), height: grow!(), color: [250, 20, 20] {} }
        }}
    }}
}

fn frame_pixels(
    app: &mut Application,
    batch: bool,
    a: &[ColoredRect],
    b: &[ColoredRect],
) -> Vec<u8> {
    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| draw_scene(batch, a, b)),
        "tick should keep running"
    );
    app.read_pixels().expect("render backend readback").pixels
}

fn assert_batch_matches_scalar(app: &mut Application, what: &str, scale: f32) {
    let a = scope_columns(60, -2.0);
    let b = scope_columns(40, 3.25);
    // Two ticks per mode so both are compared in layout steady state.
    frame_pixels(app, false, &a, &b);
    let scalar = frame_pixels(app, false, &a, &b);
    frame_pixels(app, true, &a, &b);
    let batched = frame_pixels(app, true, &a, &b);

    assert!(
        batched == scalar,
        "{what}: batched frame differs from the scalar rect loop"
    );
    let touched = scalar.chunks(3).filter(|p| **p != CLEAR).count();
    assert!(
        touched > 200,
        "{what}: the scene must actually draw (touched {touched} pixels)"
    );

    // Inside both scope_a and the cover box: the box wins, so the canvas z-order is exercised for real.
    let at = |x: f32, y: f32| {
        let i = ((y * scale) as usize * 96 + (x * scale) as usize) * 3;
        [scalar[i], scalar[i + 1], scalar[i + 2]]
    };
    let near = |p: [u8; 3], q: [u8; 3]| p.iter().zip(q).all(|(a, b)| a.abs_diff(b) <= 2);
    assert!(
        near(at(17.0, 11.0), [250, 20, 20]),
        "{what}: the later floating box covers the canvas"
    );
    // The column at x = 1 would reach y = 26 unclipped; scope_a ends at y = 24, so below it stays clear.
    assert!(
        near(at(1.5, 25.0), CLEAR),
        "{what}: columns are clipped to the canvas rect"
    );
    assert!(
        !near(at(1.5, 20.0), CLEAR),
        "{what}: the same column draws inside the canvas"
    );
}

#[test]
fn rects_batch_matches_scalar_loop() {
    let mut app = Application::new(96, 64, true);

    assert_batch_matches_scalar(&mut app, "unit scale", 1.0);
    app.set_ui_zoom(1.5);
    assert_batch_matches_scalar(&mut app, "ui zoom 1.5", 1.5);

    let mut state = ();
    assert!(app.tick(&mut state, |_: &mut ()| {
        layout! { width: grow!(), height: grow!() {
            Painter::rects(&[]);
        }}
    }));
    let pv = app.read_pixels().expect("render backend readback");
    assert!(
        pv.pixels.chunks(3).all(|p| p == CLEAR),
        "an empty batch leaves the frame clear"
    );
}
