//! The [`Application`] lifecycle: bringing the toolkit up, driving frames, and tearing it
//! back down, in window-owning or embedded mode.

use std::os::raw::c_void;
use std::ptr;
use std::sync::atomic::{AtomicU64, Ordering};

use crate::sys;

use crate::frame_thread::FrameClaim;
use crate::{
    ensure_fl_init, Arena, BaseArena, CursorShape, InputEvent, Key, MouseButton, Pixels, Renderer,
    Vec2,
};

/// Which tier an [`Application`] drives.
enum Backing {
    /// flowi owns the window, the event pump and presentation, and calls back once per frame.
    App(*mut sys::App),
    /// The caller owns the display, the input pump and presentation.
    Embedded(*mut sys::Host),
}

/// An owned flowi application: the toolkit wiring plus as much of the OS integration as
/// flowi was asked to own. [`new`](Self::new) opens a window (or an offscreen surface in
/// headless mode) and runs the frame loop; [`embedded`](Self::embedded) leaves the window,
/// the event pump and presentation to the caller and hands back pixels instead. Dropping it
/// tears the whole tier down.
///
/// The two modes share the process-wide foundation and differ only in the frame-driving
/// surface: [`run`](Self::run) / [`tick`](Self::tick) and the window and injection helpers
/// belong to the windowed mode, [`tick_embedded`](Self::tick_embedded) /
/// [`push_input`](Self::push_input) to the embedded one. Calling one mode's entry point on
/// the other panics.
///
/// Only one application can be alive in the process: the C toolkit drives a single global
/// frame context, so creating a second while one exists panics.
pub struct Application {
    backing: Backing,
    /// The process-wide foundation arena, borrowed.
    arena: BaseArena,
    /// Distinguishes this application's [`Window`]s from every other application's.
    id: u64,
    /// One boxed frame context per [`open_window`](Self::open_window) call. C holds a raw
    /// pointer into each box until fl_app_destroy, which [`Drop`] runs before this field drops.
    window_ctxs: Vec<Box<dyn std::any::Any>>,
    /// Holds the process's one frame-driving slot; declared last so it is released only
    /// after everything above is torn down.
    _claim: FrameClaim,
}

/// The id the next [`Application`] takes.
static NEXT_APPLICATION_ID: AtomicU64 = AtomicU64::new(1);

/// How to bring an embedded [`Application`](Application::embedded) up. The render size is
/// the only thing most embedding hosts set - flowi renders every frame at that size and the
/// caller presents the result.
#[derive(Copy, Clone, Debug)]
pub struct EmbeddedSettings {
    /// Render width in pixels.
    pub render_width: u32,
    /// Render height in pixels.
    pub render_height: u32,
    /// Mount the process working directory so [`Font::load`](crate::Font::load) and [`Image::load`](crate::Image::load) resolve
    /// plain filesystem paths against it. A host that mounts its own data directory and
    /// loads through [`Font::load_mount`](crate::Font::load_mount) leaves this off.
    pub mount_working_directory: bool,
    /// Background workers the job system starts. Only read by the entry point that brings
    /// the process-wide foundation up; once it is up the count is fixed for the process.
    pub job_threads: i32,
}

impl EmbeddedSettings {
    /// Settings for rendering at width x height, with no working-directory asset mount
    /// and one background job worker.
    ///
    /// A zero width or height asks for a surfaceless application: flowi brings the
    /// resource tier, the VFS and the job system up but opens no render surface and no UI
    /// context. Nothing can be drawn against one - [`tick_embedded`](Application::tick_embedded)
    /// has no frame to advance - so it is only useful to a host that wants the tier without
    /// the picture, and [`pixels`](Application::pixels) reports None there.
    pub fn new(width: u32, height: u32) -> EmbeddedSettings {
        EmbeddedSettings {
            render_width: width,
            render_height: height,
            mount_working_directory: false,
            job_threads: 1,
        }
    }

    /// Also mount the working directory for [`Font::load`](crate::Font::load) / [`Image::load`](crate::Image::load).
    pub fn with_working_directory(mut self) -> EmbeddedSettings {
        self.mount_working_directory = true;
        self
    }

    /// Start the job system with job_threads background workers instead of the default
    /// one. Ignored if some other entry point already brought the foundation up.
    pub fn with_job_threads(mut self, job_threads: i32) -> EmbeddedSettings {
        self.job_threads = job_threads;
        self
    }
}

/// How to bring a windowed or headless [`Application`] up ([`Application::with_settings`]).
/// [`new`](Self::new) holds the defaults: the software renderer and a windowed, resizable,
/// non-fullscreen main window titled "flowi"; [`with_title`](Self::with_title) sets the title.
#[derive(Copy, Clone, Debug)]
pub struct AppSettings {
    title: &'static str,
    width: u32,
    height: u32,
    headless: bool,
    renderer: Renderer,
    fullscreen: bool,
    resizable: bool,
}

impl AppSettings {
    /// A windowed application at width x height logical pixels: the main window is created
    /// at that size times the display content scale.
    pub fn new(width: u32, height: u32) -> AppSettings {
        AppSettings {
            title: "flowi",
            width,
            height,
            headless: false,
            renderer: Renderer::Software,
            fullscreen: false,
            resizable: true,
        }
    }

    /// The main window's title.
    pub fn with_title(mut self, title: &'static str) -> AppSettings {
        self.title = title;
        self
    }

    /// Render offscreen without opening a window. A headless application gets no OS input: drive it
    /// with the `inject_*` calls on [`Application`] and read frames back with
    /// [`Application::read_pixels`].
    pub fn headless(mut self, headless: bool) -> AppSettings {
        self.headless = headless;
        self
    }

    /// Select the render backend.
    pub fn with_renderer(mut self, renderer: Renderer) -> AppSettings {
        self.renderer = renderer;
        self
    }

    /// Open the main window fullscreen at the desktop resolution; the size is what it
    /// restores to.
    pub fn fullscreen(mut self, fullscreen: bool) -> AppSettings {
        self.fullscreen = fullscreen;
        self
    }

    /// Whether the user can resize the main window.
    pub fn resizable(mut self, resizable: bool) -> AppSettings {
        self.resizable = resizable;
        self
    }

    fn to_sys(self) -> sys::AppSettings {
        sys::AppSettings {
            window_title: sys::RawStr::from_static(self.title),
            width: self.width as i32,
            height: self.height as i32,
            headless: self.headless,
            renderer: self.renderer.to_sys(),
            fullscreen: self.fullscreen,
            fixed_size: !self.resizable,
        }
    }
}

/// Initial settings for a host-opened window ([`Application::open_window`]): a
/// title plus the drawable size in physical pixels. The window is backend-placed -
/// the app offsets secondary windows so they do not stack on the main one.
pub struct WindowSettings {
    title: &'static str,
    width: u32,
    height: u32,
}

impl WindowSettings {
    /// A window titled title at width x height physical pixels, backend-placed.
    pub fn new(title: &'static str, width: u32, height: u32) -> WindowSettings {
        WindowSettings {
            title,
            width,
            height,
        }
    }
}

/// A handle to a window opened with [`Application::open_window`]. Pass it to
/// [`Application::close_window`], which consumes it, to request it close. Neither `Copy` nor
/// `Clone`, so a window cannot be closed twice.
pub struct Window {
    window: *mut sys::AppWindow,
    /// The [`Application`] that opened it.
    app: u64,
}

/// Per-frame context handed to the trampoline through fl_app's single
/// `void* user_data`.
#[repr(C)]
struct FrameCtx<S, F> {
    state: S,
    frame: F,
}

/// extern "C" frame trampoline: runs the user closure once per frame and aborts the process on
/// panic, so no unwind crosses into C and no torn state is observed afterwards.
///
/// # Safety
///
/// ud must be the `FrameCtx<S, F>` that [`tramp_for`] selected this instantiation for, live and uniquely
/// borrowable for the call. fl_app hands back exactly the pointer it was registered with, and the context
/// outlives the blocking fl_app_run / fl_app_tick call it was registered for.
unsafe extern "C" fn tramp<S, F: FnMut(&mut S)>(ud: *mut c_void) {
    // SAFETY: the caller's contract - ud is the live, uniquely-borrowable `FrameCtx<S, F>` this
    // instantiation was registered with.
    let ctx = unsafe { &mut *(ud as *mut FrameCtx<S, F>) };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        (ctx.frame)(&mut ctx.state);
    }));
    if result.is_err() {
        // The default panic hook has already printed the message/backtrace.
        std::process::abort();
    }
}

/// Select the monomorphised [`tramp`] for ctx's S and F, which cannot be named at the call site.
fn tramp_for<S, F: FnMut(&mut S)>(_ctx: &FrameCtx<S, F>) -> sys::AppFrameFn {
    Some(tramp::<S, F>)
}

impl Application {
    /// Create an application at width x height logical pixels with the software renderer.
    /// headless = false opens a window titled "flowi" (use [`with_settings`](Self::with_settings)
    /// and [`AppSettings::with_title`] for another title); headless = true renders offscreen with
    /// no window, see [`AppSettings::headless`].
    ///
    /// Panics if the application fails to come up (e.g. windowed mode in a build without
    /// window support).
    pub fn new(width: u32, height: u32, headless: bool) -> Application {
        Application::with_renderer(width, height, headless, Renderer::Software)
    }

    /// Create an application selecting the render backend explicitly. [`Renderer::Software`] is always
    /// available; [`Renderer::Gpu`] requires a flowi build with GPU support and a working GL 4.3 context at
    /// runtime, and panics otherwise.
    pub fn with_renderer(
        width: u32,
        height: u32,
        headless: bool,
        renderer: Renderer,
    ) -> Application {
        Application::with_settings(
            AppSettings::new(width, height)
                .headless(headless)
                .with_renderer(renderer),
        )
    }

    /// Create a windowed application whose main window opens fullscreen on its display at the
    /// desktop resolution, so a tiling window manager leaves it alone. width x height is the size
    /// the window restores to on leaving fullscreen.
    ///
    /// Panics where [`with_renderer`](Self::with_renderer) does.
    pub fn fullscreen(width: u32, height: u32, renderer: Renderer) -> Application {
        Application::with_settings(
            AppSettings::new(width, height)
                .with_renderer(renderer)
                .fullscreen(true),
        )
    }

    /// Create an application from explicit [`AppSettings`]. The settings' width and height
    /// are logical pixels: a windowed main window is created at that size times the
    /// display content scale.
    ///
    /// Panics where [`with_renderer`](Self::with_renderer) does.
    pub fn with_settings(settings: AppSettings) -> Application {
        let claim = FrameClaim::acquire();
        let arena = ensure_fl_init();
        let settings = settings.to_sys();
        // SAFETY: settings is a live, fully-initialised value for the call, and its window_title is a
        // 'static C string; fl_app_create copies what it keeps.
        let app = unsafe { sys::fl_app_create(&settings) };
        assert!(!app.is_null(), "fl_app_create failed");
        Application {
            backing: Backing::App(app),
            arena,
            id: NEXT_APPLICATION_ID.fetch_add(1, Ordering::Relaxed),
            window_ctxs: Vec::new(),
            _claim: claim,
        }
    }

    /// The user's zoom preference, multiplied onto each window's monitor content scale to
    /// give the absolute UI scale. 1.0 - the default - is DPI-correct. A headless application
    /// has no monitor, so its content scale is 1.0 and this is its UI scale.
    ///
    /// No-op on an embedded application, which has no window and no scale policy of its own.
    pub fn set_ui_zoom(&self, zoom: f32) {
        if let Backing::App(app) = self.backing {
            // SAFETY: app is the live fl_app_create handle this Application owns.
            unsafe { sys::fl_app_set_ui_zoom(app, zoom) };
        }
    }

    /// Create an application for a caller that owns its own display, input pump and
    /// presentation - a kiosk shell, an audio plugin inside a host's window, a frontend
    /// rendering to DRM. flowi owns everything below the surface: the render backend, the
    /// resource tier, the VFS and image system, fonts, the surface and the UI context.
    ///
    /// The frame is the caller's to drive: [`tick_embedded`](Self::tick_embedded) once per
    /// frame with the UI to draw, [`pixels`](Self::pixels) to present the result, and
    /// [`push_input`](Self::push_input) to feed the OS events the caller decoded. The
    /// window-owning entry points ([`run`](Self::run), [`tick`](Self::tick),
    /// [`open_window`](Self::open_window), the `inject_*` harness) panic on one of these.
    ///
    /// [`None`] when the tier fails to come up; flowi has logged the reason by then.
    ///
    /// Shares the process-wide foundation with every other flowi entry point (see
    /// [`Application::new`]), so an embedded application and a bare
    /// [`TextInput`](crate::TextInput) can coexist.
    pub fn embedded(settings: EmbeddedSettings) -> Option<Application> {
        let claim = FrameClaim::acquire();
        let arena = crate::foundation::init(settings.job_threads);

        let sys_settings = sys::HostSettings {
            render_width: settings.render_width as i32,
            render_height: settings.render_height as i32,
            mount_working_directory: settings.mount_working_directory,
        };
        // SAFETY: the foundation arena outlives every Application - nothing in this crate
        // tears it down except the unsafe shutdown, whose contract forbids running it while an
        // Application is live. settings only has to outlive the call.
        let host = unsafe { sys::fl_host_create(arena.as_raw().cast(), &sys_settings) };
        if host.is_null() {
            return None;
        }

        // fl_host_create leaves no frame open; open the first one so the first tick_embedded
        // emits inside it. A surfaceless host has no UI context to open a frame on.
        if sys_settings.render_width > 0 && sys_settings.render_height > 0 {
            // SAFETY: a live host with a UI context and no frame open yet.
            unsafe { sys::fl_host_frame_update(host, 0.0) };
        }

        Some(Application {
            backing: Backing::Embedded(host),
            arena,
            id: NEXT_APPLICATION_ID.fetch_add(1, Ordering::Relaxed),
            window_ctxs: Vec::new(),
            _claim: claim,
        })
    }

    /// The process-wide foundation arena - where flowi's own long-lived allocations come
    /// from, and where an embedding host puts the ones that must outlive every frame.
    ///
    /// Lives exactly as long as the foundation does, which is at least as long as this
    /// [`Application`]: only [`shutdown`](crate::shutdown) takes it down, and that may not
    /// run while one is alive.
    pub fn arena(&self) -> &Arena {
        &self.arena
    }

    /// The fl_app handle, for the entry points that only the window-owning mode has.
    ///
    /// # Panics
    /// Panics on an [`embedded`](Self::embedded) application.
    fn app(&self) -> *mut sys::App {
        match self.backing {
            Backing::App(app) => app,
            Backing::Embedded(_) => panic!(
                "flowi: this Application was created with `Application::embedded`, which \
                 leaves the window, the event pump and presentation to the caller — drive \
                 it with `tick_embedded` / `pixels` / `push_input` instead"
            ),
        }
    }

    /// The fl_host handle, for the entry points that only the embedded mode has.
    ///
    /// # Panics
    /// Panics on a window-owning application.
    fn host(&self) -> *mut sys::Host {
        match self.backing {
            Backing::Embedded(host) => host,
            Backing::App(_) => panic!(
                "flowi: this Application owns its window and frame loop — drive it with \
                 `run` / `tick`, or create it with `Application::embedded` to drive the \
                 frame yourself"
            ),
        }
    }

    /// Run the frame loop, calling frame once per frame until the main window closes, then
    /// tear the application down. Blocks for the whole run.
    pub fn run<S, F: FnMut(&mut S)>(self, state: S, frame: F) {
        let mut ctx = FrameCtx { state, frame };
        let tramp = tramp_for(&ctx);
        // SAFETY: tramp_for selected the trampoline instantiation that matches ctx, satisfying tramp's
        // contract. fl_app_run blocks, so ctx stays live on this stack frame for every callback it makes,
        // and C retains neither the pointer nor the app handle afterwards.
        unsafe {
            sys::fl_app_run(self.app(), &mut ctx as *mut _ as *mut c_void, tramp);
        }
    }

    /// Drive a single frame. Returns false once a quit/window-close has been
    /// seen. Unlike [`run`](Self::run), the caller keeps ownership of state
    /// across ticks.
    pub fn tick<T, F: FnMut(&mut T)>(&mut self, state: &mut T, mut frame: F) -> bool {
        // state is already a &mut T, so the closure peels the extra reference the trampoline adds.
        let mut ctx = FrameCtx {
            state: &mut *state,
            frame: move |s: &mut &mut T| frame(&mut **s),
        };
        let tramp = tramp_for(&ctx);
        // SAFETY: tramp_for selected the trampoline instantiation that matches ctx, satisfying tramp's
        // contract. fl_app_tick blocks for the frame, so ctx stays live on this stack frame for the
        // callback it makes, and C does not retain the pointer past the call.
        unsafe { sys::fl_app_tick(self.app(), &mut ctx as *mut _ as *mut c_void, tramp) }
    }

    /// Open a second (or further) window on this app, with its own per-frame
    /// callback and state. The app ticks + presents it on every
    /// [`tick`](Self::tick) / [`run`](Self::run) until it is closed; frame runs
    /// once per frame against its own state, independent of the main window's.
    /// Returns a [`Window`] handle for [`close_window`](Self::close_window).
    ///
    /// Multiple windows need a headless or a windowed-GPU application; a windowed software
    /// application has only its main window. Panics if the open fails (e.g. on that path).
    pub fn open_window<S, F>(&mut self, settings: WindowSettings, state: S, frame: F) -> Window
    where
        S: 'static,
        F: FnMut(&mut S) + 'static,
    {
        let mut ctx = Box::new(FrameCtx { state, frame });
        let tramp = tramp_for(&*ctx);
        let user_data = &mut *ctx as *mut FrameCtx<S, F> as *mut c_void;
        let ws = sys::WindowSettings {
            title: sys::RawStr::from_static(settings.title),
            width: settings.width as i32,
            height: settings.height as i32,
            x: sys::FL_APP_WINDOW_POS_DEFAULT,
            y: sys::FL_APP_WINDOW_POS_DEFAULT,
        };
        // SAFETY: tramp_for selected the trampoline instantiation that matches `*ctx`, satisfying tramp's
        // contract, and user_data addresses that box - pushed into self.window_ctxs just below, so the
        // heap allocation outlives every callback C makes against this window. ws is read during the call
        // and its title is 'static.
        let window = unsafe { sys::fl_app_window_open(self.app(), &ws, tramp, user_data) };
        assert!(!window.is_null(), "fl_app_window_open failed");
        self.window_ctxs.push(ctx);
        Window {
            window,
            app: self.id,
        }
    }

    /// Request that a window opened with [`open_window`](Self::open_window) close.
    /// The window is torn down at the start of the next [`tick`](Self::tick), so
    /// the app keeps running and the other windows keep ticking. Closing the main
    /// window this way is a no-op (quit by letting its close reach tick).
    ///
    /// # Panics
    /// Panics if the window was opened by a different [`Application`].
    pub fn close_window(&mut self, window: Window) {
        assert_eq!(
            window.app, self.id,
            "flowi: close_window given a Window opened by a different Application"
        );
        // SAFETY: the app is borrowed here and its windows live until it is torn down. The id check
        // proves window came from this app's open_window, and Window is consumed by value, so the
        // handle names a window of this app that is closed at most once through it.
        unsafe { sys::fl_app_window_close(self.app(), window.window) };
    }

    /// Headless harness: move the injected mouse cursor to (x, y) in window pixels. Only valid for a headless
    /// app. The move is captured by the next [`tick`](Self::tick)'s input pump and
    /// becomes the hit-test position for the following frame, so move the cursor a tick before reading hover/
    /// click state on a widget.
    pub fn inject_mouse_move(&mut self, x: i32, y: i32) {
        // SAFETY: the app handle is live (borrowed here) and only scalars cross.
        unsafe { sys::fl_app_inject_mouse_move(self.app(), x, y) };
    }

    /// Headless harness: inject a mouse-button press (down = true) or release (down = false). Only valid
    /// for a headless app. Like [`inject_mouse_move`](Self::inject_mouse_move), the event is applied on the next
    /// tick and drives click detection a frame later - a press tick followed by a release tick over a hovered
    /// widget yields a Signal.clicked shortly after.
    pub fn inject_mouse_button(&mut self, button: MouseButton, down: bool) {
        // SAFETY: the app handle is live (borrowed here) and only scalars cross.
        unsafe { sys::fl_app_inject_mouse_button(self.app(), button, down) };
    }

    /// Headless harness: inject a key press (down = true) or release (down = false). Only valid for a headless
    /// app. Like [`inject_mouse_button`](Self::inject_mouse_button), the event is applied
    /// on the next [`tick`](Self::tick) and observed that frame by the [`UiInput::key_pressed`](crate::UiInput::key_pressed) queries - so a
    /// press tick makes UiInput::key_pressed(key) read true on that tick, and UiInput::key_down(key) stays
    /// true until a release is injected.
    pub fn inject_key(&mut self, key: Key, down: bool) {
        // SAFETY: the app handle is live (borrowed here) and only scalars cross.
        unsafe { sys::fl_app_inject_key(self.app(), key, down) };
    }

    /// Headless harness: inject a raw relative-motion delta - a hidden-drag scrub. Only valid for a headless app.
    /// Like [`inject_mouse_move`](Self::inject_mouse_move), the delta is captured by the next [`tick`](Self::tick)'s
    /// input pump and read that frame (scaled by the [`UiInput::mouse_hidden_drag_begin`](crate::UiInput::mouse_hidden_drag_begin) sensitivity) via
    /// [`UiInput::mouse_drag_delta`](crate::UiInput::mouse_drag_delta) - so begin the drag a tick before injecting deltas to scrub it.
    pub fn inject_mouse_delta(&mut self, delta_x: f32, delta_y: f32) {
        // SAFETY: the app handle is live (borrowed here) and only scalars cross.
        unsafe { sys::fl_app_inject_mouse_delta(self.app(), delta_x, delta_y) };
    }

    /// Headless harness: inject a mouse-wheel delta (x, y; positive y = scrolled up). Only valid for a
    /// headless app. Like [`inject_mouse_move`](Self::inject_mouse_move), the delta is captured by the next
    /// [`tick`](Self::tick)'s input pump and read that frame via [`UiInput::mouse_wheel_delta`](crate::UiInput::mouse_wheel_delta) - so inject
    /// the wheel a tick after the cursor is over the [`UiInput::scroll_target`](crate::UiInput::scroll_target) (the target must already be the hot
    /// element for the scoped read to return it).
    pub fn inject_wheel(&mut self, x: f32, y: f32) {
        // SAFETY: the app handle is live (borrowed here) and only scalars cross.
        unsafe { sys::fl_app_inject_wheel(self.app(), x, y) };
    }

    /// Headless harness: inject an OS file drop of `path` released at (x, y) in window pixels. Only valid
    /// for a headless app. Like [`inject_mouse_move`](Self::inject_mouse_move), the
    /// drop is captured by the next [`tick`](Self::tick)'s input pump and read that frame through
    /// [`UiInput::dropped_file_count`](crate::UiInput::dropped_file_count) and
    /// [`UiInput::dropped_file`](crate::UiInput::dropped_file); it is gone again on the tick after.
    pub fn inject_drop_file(&mut self, path: &str, x: f32, y: f32) {
        // SAFETY: the app handle is live (borrowed here), and the borrowed path outlives the one call it
        // crosses on - the headless backend copies the bytes into its own queue slot.
        unsafe { sys::fl_app_inject_drop_file(self.app(), sys::RawStr::borrow(path), x, y) };
    }

    /// Headless harness: feed every [`tick`](Self::tick) a fixed dt in seconds instead of the wall clock;
    /// 0.0 or less restores real timing. Only valid for a headless app. Anything flowi eases over time - dock tweens, smooth scroll, toggle animations - then advances by frame count
    /// rather than by how fast the harness runs.
    pub fn set_test_dt(&mut self, dt: f32) {
        // SAFETY: the app handle is live (borrowed here) and only scalars cross.
        unsafe { sys::fl_app_set_test_dt(self.app(), dt) };
    }

    /// Headless harness: read back the [`CursorShape`] the core flushed to the input backend on the last
    /// [`tick`](Self::tick); a value outside the real shapes reads back as [`CursorShape::Arrow`]. Only valid
    /// for a headless app. The readback companion to the `inject_*` calls, for asserting per-frame cursor
    /// resolution (a [`Cursor::set`](crate::Cursor::set) request, or the arrow fallback when none).
    pub fn headless_cursor_shape(&self) -> CursorShape {
        // SAFETY: the app handle is live (borrowed here); the C call range-checks the shape, so it only ever
        // returns a valid CursorShape discriminant.
        unsafe { sys::fl_app_headless_cursor_shape(self.app()) }
    }

    /// Headless harness: read back whether the input backend is in relative-mouse mode (a hidden drag) after the
    /// last [`tick`](Self::tick). Only valid for a headless app. The readback companion for asserting
    /// [`UiInput::mouse_hidden_drag_begin`](crate::UiInput::mouse_hidden_drag_begin) / [`UiInput::mouse_hidden_drag_end`](crate::UiInput::mouse_hidden_drag_end).
    #[inline]
    pub fn headless_relative_mouse_mode(&self) -> bool {
        // SAFETY: the app handle is live (borrowed here); the call returns a plain bool.
        unsafe { sys::fl_app_headless_relative_mouse_mode(self.app()) }
    }

    /// Headless harness: read back the most recent warp-back target the core flushed to the input backend (set on
    /// a [`UiInput::mouse_hidden_drag_end`](crate::UiInput::mouse_hidden_drag_end)) - the press point the cursor
    /// returns to when a hidden drag ends. Only valid for a headless app.
    #[inline]
    pub fn headless_last_warp(&self) -> Vec2 {
        // SAFETY: the app handle is live (borrowed here); the call returns a Copy struct.
        unsafe { sys::fl_app_headless_last_warp(self.app()) }
    }

    /// Draw and advance one embedded frame: ui emits into the open frame, then that frame
    /// closes, delta_time seconds of input are pumped in, and the next frame opens.
    /// Rendered pixels are available from [`pixels`](Self::pixels) once it returns.
    ///
    /// The embedded counterpart of [`tick`](Self::tick): ui runs directly on the caller's
    /// stack, captures whatever state it needs, and its return value comes back out.
    ///
    /// # Panics
    /// Panics on a window-owning application (use [`tick`](Self::tick) there).
    pub fn tick_embedded<R>(&mut self, delta_time: f32, ui: impl FnOnce() -> R) -> R {
        let host = self.host();
        let result = ui();
        // SAFETY: a live host; the frame boundary is entirely flowi's business.
        unsafe { sys::fl_host_frame_update(host, f64::from(delta_time)) };
        result
    }

    /// Push one decoded OS event in. An embedded application pumps its own OS events and
    /// delivers input this way; it lands in the frame state [`UiInput`](crate::UiInput) reads on the
    /// following [`tick_embedded`](Self::tick_embedded).
    ///
    /// # Panics
    /// Panics on a window-owning application, which pumps the OS itself (a headless one
    /// synthesises input with the `inject_*` harness instead).
    pub fn push_input(&self, event: InputEvent) {
        // Panics on a window-owning application.
        let _ = self.host();
        // SAFETY: the events are plain PODs and target the host's own context.
        unsafe {
            match event {
                InputEvent::Key(event) => sys::fl_input_push_key_event(event),
                InputEvent::TextInput(event) => sys::fl_input_push_text_input_event(event),
                InputEvent::Mouse(event) => sys::fl_input_push_mouse_event(event),
                InputEvent::Gamepad(event) => sys::fl_input_push_gamepad_event(event),
                InputEvent::Window(event) => sys::fl_input_push_window_event(event),
            }
        }
    }

    /// The pixels of the frame just rendered, borrowed from the render backend until the
    /// next frame overwrites them - the zero-copy sibling of [`read_pixels`](Self::read_pixels),
    /// for an embedded application presenting every frame itself. [`None`] when the render
    /// backend cannot read back. Works in either mode.
    pub fn pixels(&self) -> Option<Pixels<'_>> {
        let rb = self.pixel_readback()?;
        // SAFETY: a successful readback of a live backend, whose buffer stays valid until
        // the next frame. tick / tick_embedded take &mut self, so this shared borrow
        // excludes them for as long as the returned Pixels lives.
        Some(unsafe { Pixels::from_readback(&rb) })
    }

    /// Ask the active tier's backend for the last-rendered pixels. [`None`] when the
    /// backend has no readback capability or handed back nothing.
    fn pixel_readback(&self) -> Option<sys::PixelReadback> {
        let mut rb = sys::PixelReadback {
            pixels: ptr::null_mut(),
            width: 0,
            height: 0,
            stride: 0,
        };
        // SAFETY: a live handle of the matching tier and a live out-param.
        let ok = unsafe {
            match self.backing {
                Backing::App(app) => sys::fl_app_read_pixels(app, &mut rb),
                Backing::Embedded(host) => sys::fl_host_read_pixels(host, &mut rb),
            }
        };
        (ok && !rb.pixels.is_null()).then_some(rb)
    }

    /// Read back the current frame's rendered pixels as an owned, tightly-packed
    /// copy; [`None`] when the render backend cannot read back.
    pub fn read_pixels(&mut self) -> Option<PixelView> {
        let rb = self.pixel_readback()?;

        let width = rb.width;
        let height = rb.height;
        let stride = rb.stride as usize;
        let channels = pixel_readback_channels(width, stride);
        let row_bytes = width as usize * channels;
        let mut pixels = Vec::with_capacity(row_bytes * height as usize);
        let base = rb.pixels as *const u8;
        for y in 0..height as usize {
            // SAFETY: the backend guarantees height rows of at least row_bytes
            // valid bytes each, spaced stride apart; we copy out before the
            // buffer is invalidated.
            let row = unsafe { std::slice::from_raw_parts(base.add(y * stride), row_bytes) };
            pixels.extend_from_slice(row);
        }
        Some(PixelView {
            width,
            height,
            channels,
            pixels,
        })
    }
}

impl Drop for Application {
    fn drop(&mut self) {
        // Runs before window_ctxs drops, so C never holds a freed per-window context.
        match self.backing {
            // SAFETY: created by with_settings and non-null, destroyed exactly once here.
            Backing::App(app) => unsafe { sys::fl_app_destroy(app) },
            // SAFETY: created by embedded and non-null, destroyed exactly once here.
            Backing::Embedded(host) => unsafe { sys::fl_host_destroy(host) },
        }
    }
}

/// Derive the channel count of a pixel readback from its row stride. Readback rows are tightly
/// packed (`stride == width * channels`: 3 for the software backend, 4 for the GPU one), so a stride
/// that is not a whole multiple of width panics.
fn pixel_readback_channels(width: u32, stride: usize) -> usize {
    if width == 0 {
        return 0;
    }
    let width = width as usize;
    assert!(
        stride.is_multiple_of(width),
        "read_pixels: row stride {stride} is not a whole multiple of width {width}; \
         the render backend violated the tightly-packed readback contract"
    );
    stride / width
}

/// An owned snapshot of a frame, tightly packed: `pixels.len() == width * height * channels`. channels is 3
/// for the software backend (RGB24) and 4 for the GPU backend (RGBA8); read it to interpret pixels.
pub struct PixelView {
    pub width: u32,
    pub height: u32,
    pub channels: usize,
    pub pixels: Vec<u8>,
}

#[cfg(test)]
mod tests {
    use super::{pixel_readback_channels, AppSettings, Renderer};

    #[test]
    fn derives_channels_from_tight_stride() {
        assert_eq!(pixel_readback_channels(64, 64 * 3), 3);
        assert_eq!(pixel_readback_channels(64, 64 * 4), 4);
        assert_eq!(pixel_readback_channels(37, 37 * 3), 3);
    }

    #[test]
    fn zero_width_yields_zero_channels() {
        assert_eq!(pixel_readback_channels(0, 0), 0);
    }

    #[test]
    #[should_panic(expected = "tightly-packed readback contract")]
    fn padded_stride_panics_rather_than_truncating() {
        // A padded row (100 px RGB24 = 300 bytes rounded up to 302) is not a
        // whole multiple of width; must fail loudly, not derive 3 and misread.
        let _ = pixel_readback_channels(100, 302);
    }

    #[test]
    fn settings_defaults_match_the_forwarding_constructors() {
        let sys = AppSettings::new(640, 480).to_sys();
        assert_eq!((sys.width, sys.height), (640, 480));
        assert!(!sys.headless && !sys.fullscreen);
        assert!(!sys.fixed_size);
        assert_eq!(sys.renderer, Renderer::Software.to_sys());
    }

    #[test]
    fn settings_builders_reach_the_c_struct() {
        let sys = AppSettings::new(10, 20)
            .headless(true)
            .with_renderer(Renderer::Gpu)
            .fullscreen(true)
            .resizable(false)
            .to_sys();
        assert!(sys.headless && sys.fullscreen && sys.fixed_size);
        assert_eq!(sys.renderer, Renderer::Gpu.to_sys());
    }
}
