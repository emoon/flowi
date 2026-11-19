//! Input-driving helpers shared by the headless integration tests.

#![allow(dead_code)]

use flowi::{Application, ElementState, MouseButton, PixelView, Vec2};

/// Runs `n` ticks of `frame`, asserting each one keeps the application running.
pub fn tick_n<T>(
    app: &mut Application,
    state: &mut T,
    mut frame: impl FnMut(&mut T),
    n: usize,
    what: &str,
) {
    for _ in 0..n {
        assert!(
            app.tick(state, &mut frame),
            "{what} tick should keep running"
        );
    }
}

/// Moves the cursor to `(x, y)`, holds it for two ticks so hot state settles, then presses and releases
/// `button`, ticking once per edge.
pub fn press_release_at<T>(
    app: &mut Application,
    state: &mut T,
    frame: fn(&mut T),
    button: MouseButton,
    (x, y): (i32, i32),
) {
    app.inject_mouse_move(x, y);
    assert!(app.tick(state, frame), "hover tick 1");
    assert!(app.tick(state, frame), "hover tick 2");
    app.inject_mouse_button(button, true);
    assert!(app.tick(state, frame), "{button:?} press");
    app.inject_mouse_button(button, false);
    assert!(app.tick(state, frame), "{button:?} release");
}

/// Left-clicks at `(x, y)` in window pixels.
pub fn click_at<T>(app: &mut Application, state: &mut T, frame: fn(&mut T), x: i32, y: i32) {
    press_release_at(app, state, frame, MouseButton::Left, (x, y));
}

/// Right-clicks at `(x, y)` in window pixels.
pub fn right_click_at<T>(app: &mut Application, state: &mut T, frame: fn(&mut T), x: i32, y: i32) {
    press_release_at(app, state, frame, MouseButton::Right, (x, y));
}

/// Centre of `e`'s rect, truncated to whole window pixels.
pub fn center(e: ElementState) -> (i32, i32) {
    let Vec2 { x, y } = e.pos;
    ((x + e.size.x * 0.5) as i32, (y + e.size.y * 0.5) as i32)
}

/// Default clear color (theme surface.base).
pub const CLEAR: [u8; 3] = [14, 14, 16];

/// An RGB24 readback of the current frame.
pub struct Readback(PixelView);

impl Readback {
    /// Reads the frame back, asserting the software backend's RGB24 layout.
    pub fn capture(app: &mut Application) -> Self {
        let pv = app.read_pixels().expect("render backend readback");
        assert_eq!(pv.channels, 3, "software backend reads back RGB24");
        Self(pv)
    }

    pub fn byte_len(&self) -> usize {
        self.0.pixels.len()
    }

    pub fn width(&self) -> usize {
        self.0.width as usize
    }

    pub fn height(&self) -> usize {
        self.0.height as usize
    }

    pub fn px(&self, x: usize, y: usize) -> [u8; 3] {
        let i = (y * self.width() + x) * 3;
        [self.0.pixels[i], self.0.pixels[i + 1], self.0.pixels[i + 2]]
    }

    /// Number of non-clear pixels in `[x0, x1) x [y0, y1)`.
    pub fn inked_in(&self, (x0, y0): (usize, usize), (x1, y1): (usize, usize)) -> usize {
        (y0..y1)
            .flat_map(|y| (x0..x1).map(move |x| (x, y)))
            .filter(|&(x, y)| self.px(x, y) != CLEAR)
            .count()
    }

    /// Number of non-clear pixels and the brightest of them (first wins on ties).
    pub fn ink_and_brightest(&self) -> (usize, [u8; 3]) {
        let brightness = |p: [u8; 3]| p.iter().map(|&c| c as u16).sum::<u16>();
        let mut brightest = [0u8; 3];
        let mut ink = 0usize;
        for y in 0..self.height() {
            for x in 0..self.width() {
                let p = self.px(x, y);
                if p == CLEAR {
                    continue;
                }
                ink += 1;
                if brightness(p) > brightness(brightest) {
                    brightest = p;
                }
            }
        }
        (ink, brightest)
    }
}
