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

extern "Rust" {
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
unsafe extern "C" fn mainloop_trampoline_ud<T>(_ctx: *const c_void, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let f: &&(dyn Fn(&mut T) + 'static) = transmute(wd.func);
    let data = wd.user_data as *mut T;
    f(&mut *data);
}

#[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
unsafe extern "C" fn mainloop_trampoline(_ctx: *const c_void, user_data: *mut c_void) {
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
                crate::init_function_ptrs(api);
                Ok(Self { api })
            }
        }
    }

    #[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
    pub fn main_loop_ud<'a, F, T>(&self, data: &'a mut T, func: F) -> bool
    where
        F: Fn(&mut T) + 'a,
        T: 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&mut T) + 'a>> = Box::new(Box::new(func));
        let user_data = data as *const _ as *const c_void;
        let api = unsafe { &*self.api };

        let wrapped_data = WrappedMainData {
            priv_data: api.data,
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

    #[cfg(any(feature = "dynamic", feature = "static", feature = "tundra"))]
    pub fn main_loop<'a, F>(&self, func: F) -> bool
    where
        F: Fn() + 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn() + 'a>> = Box::new(Box::new(func));
        let api = unsafe { &*self.api };

        let wrapped_data = WrappedMainData {
            priv_data: api.data,
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

