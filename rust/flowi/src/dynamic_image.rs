//! The dynamic-image widget: an owned RGBA pixel buffer the caller refreshes each frame, and the widget
//! that draws it.

use crate::sys;

use crate::frame_thread::frame_thread_guard;
use crate::ImageOpts;

/// An owned, fixed-size, tightly packed RGBA8 pixel buffer for [`DynamicImage::new`]. The buffer never
/// reallocates or moves its pixels, so its address is stable for the buffer's whole life - the renderer
/// identifies a dynamic image by that address across frames.
pub struct DynamicImageBuffer {
    pixels: Box<[u8]>,
    width: u32,
    height: u32,
}

impl DynamicImageBuffer {
    /// A transparent-black buffer of width x height pixels.
    ///
    /// # Panics
    /// Panics when either dimension exceeds `i32::MAX / 4` or the byte size overflows `usize`.
    pub fn new(width: u32, height: u32) -> Self {
        assert!(
            width <= i32::MAX as u32 / 4 && height <= i32::MAX as u32 / 4,
            "dynamic image dimensions out of range"
        );
        let len = (width as usize)
            .checked_mul(height as usize)
            .and_then(|n| n.checked_mul(4))
            .expect("dynamic image byte size overflows usize");
        DynamicImageBuffer {
            pixels: vec![0u8; len].into_boxed_slice(),
            width,
            height,
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

    /// The pixels, row-major RGBA8 with no row padding: `width * height * 4` bytes.
    pub fn pixels(&self) -> &[u8] {
        &self.pixels
    }

    /// The pixels for writing, row-major RGBA8 with no row padding: `width * height * 4` bytes.
    pub fn pixels_mut(&mut self) -> &mut [u8] {
        &mut self.pixels
    }
}

/// Dynamic image widget - draws a [`DynamicImageBuffer`] the caller refreshes each frame.
pub struct DynamicImage;

impl DynamicImage {
    /// Draw buffer as a layout widget. opts tints it and selects fill-container scaling. Call inside a
    /// frame callback, after writing this frame's pixels.
    ///
    /// The renderer reads the pixels after the frame callback returns, straight out of the buffer.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    ///
    /// # Safety
    /// buffer must stay alive, and must not be written, from this call until the frame it was drawn in has
    /// been rendered: the return of the [`Application::tick`](crate::Application::tick) or
    /// [`Application::tick_embedded`](crate::Application::tick_embedded) call that ran the frame, or the start
    /// of the next frame callback under [`Application::run`](crate::Application::run).
    #[allow(clippy::new_ret_no_self)]
    pub unsafe fn new(buffer: &DynamicImageBuffer, opts: ImageOpts) {
        let _frame = frame_thread_guard("DynamicImage::new");
        let mut image = sys::DynamicImage {
            user_buffer: buffer.pixels.as_ptr() as *mut u8,
            width: buffer.width as i32,
            height: buffer.height as i32,
            user_stride: (buffer.width * 4) as i32,
            is_alpha_only: false,
        };
        // SAFETY: image is a live descriptor for the call, which copies its fields; user_buffer points at
        // width * height * 4 bytes the buffer owns, the caller upholds the lifetime contract above for
        // the renderer's later reads, and _frame keeps the call to the frame context's thread.
        unsafe { sys::fl_dynamic_image(&mut image, opts) }
    }
}
