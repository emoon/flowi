use flowi_core::Instance;
use flowi_core::ApplicationSettings;
use core::ptr::null_mut;
use flowi_core::Result;
use core::{ffi::c_void, mem::transmute};
use raw_window_handle::RawWindowHandle;
use crate::bgfx_renderer::BgfxRenderer;
use crate::glfw_window::GlfwWindow;

pub(crate) trait Renderer {
    fn new(settings: &ApplicationSettings, window: &RawWindowHandle) -> Self 
        where Self: Sized;
    fn render(&mut self);
}

pub(crate) trait Window {
    fn new(settings: &ApplicationSettings) -> Self 
        where Self: Sized;
    fn update(&mut self);
    fn should_close(&mut self) -> bool;
    fn is_focused(&self) -> bool;
    fn raw_window_handle(&self) -> RawWindowHandle;
}

#[repr(C)]
struct WrappedMainData {
    user_data: *const c_void,
    user_func: *const c_void,
}

#[repr(C)]
pub struct Application {
    pub(crate) renderer: Box<dyn Renderer>,
    pub(crate) window: Box<dyn Window>,
    pub(crate) core: Instance, 
    pub(crate) user: WrappedMainData,
}

unsafe extern "C" fn user_trampoline_ud<T>(wd: &WrappedMainData) {
    let f: &&(dyn Fn(&mut T) + 'static) = transmute(wd.user_func);
    let data = wd.user_data as *mut T;
    f(&mut *data);
}

unsafe extern "C" fn mainloop_app<T>(user_data: *mut c_void) {
    let state: &mut Application = transmute(user_data);
    
    while !state.window.should_close() {
        state.core.pre_update();
        state.window.update();

        user_trampoline_ud::<T>(&mut state.user);

        state.core.post_update();
        state.renderer.render();

        // TODO: This is a hack to not use 100% CPU
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}

impl Application {
    pub fn new(settings: &ApplicationSettings) -> Result<Box<Self>> {
        let core = Instance::new(settings); 
        let window = Box::new(GlfwWindow::new(settings));
        let renderer = Box::new(BgfxRenderer::new(settings, &window.raw_window_handle()));

        Ok(Box::new(Self {
            window,
            renderer,
            core,
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

