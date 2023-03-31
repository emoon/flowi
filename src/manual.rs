use crate::{
    application_settings::ApplicationSettings,
    AppFfi,
    //Flowi,
};

use core::{
    ffi::c_void,
    fmt::{Debug, Formatter},
};

#[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
use std::mem::transmute;

extern "C" {
    #[cfg(any(feature = "static", feature = "tundra"))]
    fn fl_application_create_impl(settings: *const ApplicationSettings) -> *const AppFfi;
}

pub(crate) type Mainloop = unsafe extern "C" fn(data: *const c_void, user_data: *mut c_void);

#[repr(C)]
pub(crate) struct WrappedMainData {
    pub(crate) priv_data: *const c_void,
    pub(crate) user_data: *const c_void,
    pub(crate) func: *const c_void,
}

pub struct Application {
    api: *const AppFfi,
}

#[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
unsafe extern "C" fn mainloop_trampoline_ud<T>(ctx: *const c_void, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let f: &&(dyn Fn(&mut T) + 'static) = transmute(wd.func);
    let data = wd.user_data as *mut T;
    f(&mut *data);
}

#[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
unsafe extern "C" fn mainloop_trampoline(ctx: *const c_void, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let f: &&(dyn Fn() + 'static) = transmute(wd.func);
    f();
}

impl Application {
    #[cfg(any(feature = "static", feature = "tundra"))]
    pub fn new(settings: &ApplicationSettings) -> Result<Self> {
        unsafe {
            let api = fl_application_create_impl(settings);
            if api.is_null() {
                Err(get_last_error())
            } else {
                init_function_ptrs(api);
                Ok(Self { api })
            }
        }
    }

    #[cfg(feature = "dynamic")]
    pub fn new_from_lib(path: &str, settings: &ApplicationSettings) -> Result<Self> {
        unsafe {
            // TODO: must store the lib
            let lib = libloading::Library::new(path).unwrap();
            let func: libloading::Symbol<
                unsafe extern "C" fn(*const ApplicationSettings) -> *const AppFfi,
            > = lib.get(b"fl_application_create_impl").unwrap();
            let api = func(settings);

            if api.is_null() {
                Err(get_last_error())
            } else {
                init_function_ptrs(api);
                Ok(Self { api })
            }
        }
    }

    #[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
    pub fn main_loop_ud<'a, F, T>(&self, data: &'a mut T, func: F) -> bool
    where
        F: Fn(&Flowi, &mut T) + 'a,
        T: 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&Flowi, &mut T) + 'a>> = Box::new(Box::new(func));
        let user_data = data as *const _ as *const c_void;
        let api = unsafe { &*self.api };

        let wrapped_data = WrappedMainData {
            priv_data: api.priv_data,
            user_data,
            func: Box::into_raw(f) as *const _,
        };

        unsafe {
            (api.main_loop)(
                transmute(mainloop_trampoline_ud::<T> as usize),
                transmute(&wrapped_data),
            )
        }
    }

    pub fn io(&self) -> IoApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.io_get_api)(api_priv.priv_data, 0) };
        IoApi { api }
    }

    #[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
    pub fn main_loop<'a, F>(&self, func: F) -> bool
    where
        F: Fn(&Flowi) + 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&Flowi) + 'a>> = Box::new(Box::new(func));
        let api = unsafe { &*self.api };

        let wrapped_data = WrappedMainData {
            priv_data: api.priv_data,
            user_data: std::ptr::null(),
            func: Box::into_raw(f) as *const _,
        };

        unsafe {
            (api.main_loop)(
                transmute(mainloop_trampoline as usize),
                transmute(&wrapped_data),
            )
        }
    }
}

#[repr(C)]
pub struct FlString {
    string: *const c_void,
    length: u32,
}

impl FlString {
    pub fn new(s: &str) -> Self {
        FlString {
            string: s.as_ptr() as *const c_void,
            length: s.len() as u32,
        }
    }

    pub fn as_str(&self) -> &str {
        let s =
            unsafe { std::slice::from_raw_parts(self.string as *const u8, self.length as usize) };
        std::str::from_utf8(s).unwrap()
    }
}

#[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
impl Debug for FlString {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let s =
            unsafe { std::slice::from_raw_parts(self.string as *const u8, self.length as usize) };
        let s = std::str::from_utf8(s).unwrap();
        write!(f, "{}", s)
    }
}

#[derive(Debug)]
pub struct FlowiError {
    pub message: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

pub fn get_last_error() -> FlowiError {
    // TODO: Implement
    FlowiError { message: 0 }
}

pub type Result<T> = core::result::Result<T, FlowiError>;
