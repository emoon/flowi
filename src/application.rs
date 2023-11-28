use flowi_core::Instance;
use flowi_core::ApplicationSettings;
use core::ptr::null_mut;
//use flowi_core::Mainloop;
use flowi_core::Result;
use core::{ffi::c_void, mem::transmute};
use glfw::{Action, Key, Window};
use bgfx::*;
use bgfx_rs::bgfx;
use raw_window_handle::{HasRawWindowHandle, RawWindowHandle};

#[repr(C)]
struct WrappedMainData {
    user_data: *const c_void,
    user_func: *const c_void,
}

#[repr(C)]
pub struct Application {
    core: Instance, 
    glfw: glfw::Glfw,
    events: std::sync::mpsc::Receiver<(f64, glfw::WindowEvent)>,
    window: glfw::Window,
    user: WrappedMainData,
}

fn get_platform_data(window: &Window) -> PlatformData {
    let mut pd = PlatformData::new();

    match window.raw_window_handle() {
        #[cfg(any(
            target_os = "linux",
            target_os = "dragonfly",
            target_os = "freebsd",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        RawWindowHandle::Xlib(data) => {
            pd.nwh = data.window as *mut _;
        }
        #[cfg(any(
            target_os = "linux",
            target_os = "dragonfly",
            target_os = "freebsd",
            target_os = "netbsd",
            target_os = "openbsd"
        ))]
        RawWindowHandle::Wayland(data) => {
            pd.ndt = data.surface; // same as window, on wayland there ins't a concept of windows
        }

        #[cfg(target_os = "macos")]
        RawWindowHandle::AppKit(data) => {
            pd.nwh = data.ns_window;
        }
        #[cfg(target_os = "windows")]
        RawWindowHandle::Win32(data) => {
            pd.nwh = data.hwnd;
        }
        _ => panic!("Unsupported Window Manager"),
    }

    return pd;
}

unsafe extern "C" fn user_trampoline_ud<T>(wd: &WrappedMainData) {
    let f: &&(dyn Fn(&mut T) + 'static) = transmute(wd.user_func);
    let data = wd.user_data as *mut T;
    f(&mut *data);
}


unsafe extern "C" fn mainloop_app<T>(user_data: *mut c_void) {
    let state: &mut Application = transmute(user_data);
    
    let mut old_size = (0, 0);

    while !state.window.should_close() {
        state.glfw.poll_events();
        for (_, event) in glfw::flush_messages(&state.events) {
            if let glfw::WindowEvent::Key(Key::Escape, _, Action::Press, _) = event {
                state.window.set_should_close(true)
            }
        }

        let size = state.window.get_framebuffer_size();

        if old_size != size {
            bgfx::reset(size.0 as _, size.1 as _, ResetArgs::default());
            old_size = size;
        }

        bgfx::set_view_rect(0, 0, 0, size.0 as _, size.1 as _);
        bgfx::touch(0);

        bgfx::dbg_text_clear(DbgTextClearArgs::default());

        bgfx::dbg_text(0, 1, 0x0f, "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");
        bgfx::dbg_text(80, 1, 0x0f, "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
        bgfx::dbg_text(80, 2, 0x0f, "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");
        bgfx::dbg_text(
            0,
            4,
            0x3f,
            "Description: Initialization and debug text with bgfx-rs Rust API.",
        );

        bgfx::frame(false);

        // stuff 
        user_trampoline_ud::<T>(&mut state.user);
    }
}

#[cfg(target_os = "linux")]
fn get_render_type() -> RendererType {
    RendererType::OpenGL
}

#[cfg(not(target_os = "linux"))]
fn get_render_type() -> RendererType {
    RendererType::Count
}

impl Application {
    pub fn new(settings: &ApplicationSettings) -> Result<Box<Self>> {
        //let mut init = bgfx::Init::new();
        let core = Instance::new(settings); 

        let mut glfw = glfw::init(glfw::FAIL_ON_ERRORS).unwrap();
        glfw.window_hint(glfw::WindowHint::ClientApi(glfw::ClientApiHint::NoApi));

        let width = core::cmp::max(settings.width as _, 800);
        let height = core::cmp::max(settings.height as _, 600);

        let (mut window, events) = glfw
            .create_window(width, height, "Flowi", glfw::WindowMode::Windowed)
            .expect("Failed to create GLFW window.");

        window.set_key_polling(true);

        let mut init = Init::new();

        init.type_r = get_render_type();
        init.resolution.width = width;
        init.resolution.height = height;
        init.resolution.reset = ResetFlags::VSYNC.bits();
        init.platform_data = get_platform_data(&window);

        if !bgfx::init(&init) {
            panic!("failed to init bgfx");
        }

        bgfx::set_debug(DebugFlags::TEXT.bits());
        bgfx::set_view_clear(
            0,
            ClearFlags::COLOR.bits() | ClearFlags::DEPTH.bits(),
            SetViewClearArgs {
                rgba: 0x103030ff,
                ..Default::default()
            },
        );

        /*
        init.type_r = RendererType::OpenGL;
        init.resolution.width = settings.width;
        init.resolution.height = settings.height;
        init.resolution.reset = ResetFlags::VSYNC.bits();
        init.platform_data = get_platform_data(c_data);

        // TODO: Result
        if !bgfx::init(&init) {
            panic!("failed to init bgfx");
        }
        */

        Ok(Box::new(Self {
            glfw,
            window,
            core,
            events,
            user: WrappedMainData { user_data: null_mut(), user_func: null_mut() }, 
        }))
    }

    pub fn run<'a, F, T>(&mut self, data: Box<T>, func: F) -> bool
    where
        F: Fn(&mut T) + 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&mut T) + 'a>> = Box::new(Box::new(func));
        let func = Box::into_raw(f) as *const _;

        self.user.user_data = Box::into_raw(data) as *const _;
        self.user.user_func = func;

        /*
        * TODO: If web target we should use emscripten_set_main_loop_arg
        unsafe {
            emscripten_set_main_loop_arg(
                mainloop_trampoline_ud::<T>,
                Box::into_raw(wrapped_data) as *const _,
                0,
                1,
            );
        }
        */
        unsafe { mainloop_app::<T>(self as *mut _ as *mut c_void) };

        true
    }
}

