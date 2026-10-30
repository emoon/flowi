//! What an embedding host hands flowi and gets back: decoded OS events in, rendered
//! pixels out.
//!
//! [`Application`](crate::Application) is flowi's one application abstraction. A caller
//! that already owns its window, event pump and presentation - a kiosk shell, an audio
//! plugin inside a host's window, a frontend rendering to DRM on bare metal - brings it
//! up with [`Application::embedded`](crate::Application::embedded) and drives the frame
//! itself.
//!
//! The two types here are that mode's vocabulary:
//!
//! - [`InputEvent`] is one decoded OS event, as
//!   [`Application::push_input`](crate::Application::push_input) takes it.
//! - [`Pixels`] is the frame flowi just rendered, as
//!   [`Application::pixels`](crate::Application::pixels) hands it back.
//!
//! ```no_run
//! # fn draw() {}
//! # fn present(_: &[u8]) {}
//! let mut app = flowi::Application::embedded(flowi::EmbeddedSettings::new(1280, 720))
//!     .expect("flowi application");
//!
//! loop {
//!     app.tick_embedded(1.0 / 60.0, draw);
//!     if let Some(pixels) = app.pixels() {
//!         present(pixels.row(0));
//!     }
//! }
//! ```

use crate::sys;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// One decoded OS event, as [`Application::push_input`](crate::Application::push_input)
/// takes it. The payload types are re-exported from the crate root.
///
/// Does not implement Debug: the mouse, gamepad and window payloads are untagged unions.
#[derive(Copy, Clone)]
#[non_exhaustive]
pub enum InputEvent {
    Key(sys::KeyEvent),
    TextInput(sys::TextInputEvent),
    Mouse(sys::MouseEvent),
    Gamepad(sys::GamepadEvent),
    Window(sys::WindowEvent),
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The rendered pixels of one frame, borrowed from the render backend until the next
/// frame overwrites them.
///
/// The layout is the backend's native interleaved format - RGB24 for the software
/// renderer, RGBA8 for a GPU one - with [`stride`](Self::stride) bytes per row, which
/// may exceed the row's own width.
pub struct Pixels<'a> {
    bytes: &'a [u8],
    width: u32,
    height: u32,
    stride: u32,
}

impl<'a> Pixels<'a> {
    /// Borrow a backend readback as [`Pixels`], for
    /// [`Application::pixels`](crate::Application::pixels).
    ///
    /// # Safety
    /// readback must be a successful readback of a live backend: pixels non-null and
    /// addressing at least `stride * height` bytes that stay valid for 'a.
    pub(crate) unsafe fn from_readback(readback: &sys::PixelReadback) -> Pixels<'a> {
        let len = readback.stride as usize * readback.height as usize;
        Pixels {
            // SAFETY: the caller's contract - pixels is non-null and addresses at least `stride * height`
            // bytes that stay valid for 'a.
            bytes: unsafe { core::slice::from_raw_parts(readback.pixels.cast::<u8>(), len) },
            width: readback.width,
            height: readback.height,
            stride: readback.stride,
        }
    }

    /// Width in pixels.
    pub fn width(&self) -> u32 {
        self.width
    }

    /// Height in pixels.
    pub fn height(&self) -> u32 {
        self.height
    }

    /// Bytes per row, padding included.
    pub fn stride(&self) -> u32 {
        self.stride
    }

    /// Row y, [`stride`](Self::stride) bytes long.
    ///
    /// # Panics
    /// Panics if y is past the last row.
    pub fn row(&self, y: u32) -> &'a [u8] {
        let start = y as usize * self.stride as usize;
        &self.bytes[start..start + self.stride as usize]
    }

    /// Every row, top to bottom.
    pub fn rows(&self) -> impl Iterator<Item = &'a [u8]> + '_ {
        (0..self.height).map(|y| self.row(y))
    }
}
