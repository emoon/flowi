//! What an embedding host hands flowi and gets back: decoded OS events in, rendered
//! pixels out.
//!
//! [`Application`](crate::Application) is flowi's one application abstraction. A caller
//! that already owns its window, event pump and presentation (a kiosk shell, an audio
//! plugin inside a host's window, a frontend rendering to DRM on bare metal) brings it
//! up with [`Application::embedded`](crate::Application::embedded) and drives the frame
//! itself. That mode uses the two types here:
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
//!     app.begin_frame(1.0 / 60.0);
//!     draw();
//!     app.end_frame();
//!     if let Some(pixels) = app.pixels() {
//!         present(pixels.row(0));
//!     }
//! }
//! ```

use crate::sys;

/// One decoded OS event, as [`Application::push_input`](crate::Application::push_input)
/// takes it. The payload types are re-exported from the crate root.
///
/// The mouse, gamepad and window events are safe enums whose variants carry their payload, so building
/// and reading one takes no `unsafe`. The lifetime is the borrow of a dropped file's path.
#[derive(Copy, Clone, Debug)]
#[non_exhaustive]
pub enum InputEvent<'a> {
    Key(sys::KeyEvent),
    TextInput(sys::TextInputEvent),
    Mouse(sys::SafeMouseEvent),
    Gamepad(sys::SafeGamepadEvent),
    Window(sys::SafeWindowEvent<'a>),
}

/// The rendered pixels of one frame, borrowed from the render backend until the next
/// frame overwrites them.
///
/// The layout is the backend's native interleaved format (RGB24 for the software
/// renderer, RGBA8 for a GPU one) with [`stride`](Self::stride) bytes per row, which
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
            // SAFETY: by the caller's contract, pixels is non-null and addresses at least
            // `stride * height` bytes that stay valid for 'a.
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

/// Who a gamepad is, as [`SdlEventPump`] reports it with each raw pad event.
#[derive(Copy, Clone, Debug)]
pub struct PadInfo<'a> {
    /// The name the pad reports.
    pub name: &'a str,
    /// The USB vendor id (0 if unknown).
    pub vendor: u16,
    /// The USB product id (0 if unknown).
    pub product: u16,
    /// The face-button family of the pad.
    pub pad_type: sys::PadType,
    /// Whether the pad has a Guide button.
    pub has_guide: bool,
}

/// Receives each gamepad event of an [`SdlEventPump`] with raw pads on.
///
/// A panic inside it aborts the process, as it would in any `extern "C"` callback.
pub type PadCallback = Box<dyn FnMut(sys::SafeGamepadEvent, PadInfo<'_>)>;

/// How to bring an [`SdlEventPump`] up.
pub struct SdlPumpConfig<'a> {
    /// The SDL id of the window whose events the pump reads.
    pub window_id: u32,
    /// Text of an SDL controller database, applied to pads opened afterwards (empty for none).
    pub controller_db: &'a str,
    /// Called with every gamepad event and the pad it came from. When set, no gamepad event reaches the
    /// application's own gamepad state, and pads are read by button position.
    pub on_pad: Option<PadCallback>,
}

/// Moves the SDL events of an embedding host's window into its [`Application`](crate::Application).
///
/// Build it after SDL's video subsystem is up and call
/// [`Application::pump_sdl_events`](crate::Application::pump_sdl_events) once per frame before
/// [`begin_frame`](crate::Application::begin_frame).
pub struct SdlEventPump {
    raw: *mut sys::SdlPump,
    // The `Box<PadCallback>` leaked to C as user data; freed in `Drop` after the pump is destroyed.
    on_pad: *mut PadCallback,
}

unsafe extern "C" fn pad_trampoline(
    user_data: *mut core::ffi::c_void,
    event: *const sys::GamepadEvent,
    pad: *const sys::PadIdentity,
) {
    // SAFETY: user_data is the `Box<PadCallback>` the pump was created with, alive until the pump is
    // dropped, and C calls this only from inside `pump_sdl_events`, which holds `&mut` to the pump.
    let callback = unsafe { &mut *user_data.cast::<PadCallback>() };
    // SAFETY: C passes a whole, initialized event that is valid until this call returns.
    let event = match unsafe { sys::SafeGamepadEvent::try_from_raw(event) } {
        Ok(event) => event,
        Err(err) => {
            log::error!("flowi: dropped a pad event with an invalid value: {err:?}");
            return;
        }
    };
    // SAFETY: C passes a valid identity whose name is valid until this call returns.
    let pad = unsafe { &*pad };
    // SAFETY: as above, the name outlives the call, and `info` does not escape it.
    let name = unsafe { pad.name.as_str() };
    callback(
        event,
        PadInfo {
            name,
            vendor: pad.vendor,
            product: pad.product,
            pad_type: pad.pad_type,
            has_guide: pad.has_guide,
        },
    );
}

impl SdlEventPump {
    pub(crate) fn raw(&self) -> *mut sys::SdlPump {
        self.raw
    }

    /// Creates the pump, or [`None`] if SDL's event or controller subsystem would not start; flowi has
    /// logged the reason.
    pub fn new(app: &crate::Application, config: SdlPumpConfig<'_>) -> Option<SdlEventPump> {
        let on_pad = config.on_pad.map_or(core::ptr::null_mut(), |callback| {
            Box::into_raw(Box::new(callback))
        });
        let settings = sys::SdlPumpSettings {
            window_id: config.window_id,
            raw_pads: !on_pad.is_null(),
            controller_db: sys::RawStr::borrow(config.controller_db),
        };
        let pad_fn: sys::SdlPadFn = if on_pad.is_null() {
            None
        } else {
            Some(pad_trampoline)
        };
        // SAFETY: the foundation arena outlives the application and so the pump; settings only has to
        // outlive the call; the callback box is kept in the returned value for as long as C may use it.
        let raw = unsafe {
            sys::fl_sdl_pump_create(
                app.arena().as_raw().cast(),
                &settings,
                pad_fn,
                on_pad.cast(),
            )
        };
        if raw.is_null() {
            if !on_pad.is_null() {
                // SAFETY: made by `Box::into_raw` above and not kept by C, since creation failed.
                drop(unsafe { Box::from_raw(on_pad) });
            }
            return None;
        }
        Some(SdlEventPump { raw, on_pad })
    }
}

impl Drop for SdlEventPump {
    fn drop(&mut self) {
        // SAFETY: created by `new`, non-null, destroyed exactly once here.
        unsafe { sys::fl_sdl_pump_destroy(self.raw) };
        if !self.on_pad.is_null() {
            // SAFETY: made by `Box::into_raw` in `new`; C no longer calls it once the pump is destroyed.
            drop(unsafe { Box::from_raw(self.on_pad) });
        }
    }
}
