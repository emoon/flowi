use crate::application::Application;
use crate::Flowi;
use core::{
    ffi::c_void,
    fmt::{Debug, Formatter},
};
use std::mem::transmute;

extern "C" {
    fn fl_application_main_loop_impl(callback: *const c_void, userdata: *mut c_void) -> bool;
}

#[repr(C)]
struct WrappedMainData {
    user_data: *const c_void,
    func: *const c_void,
}

unsafe extern "C" fn mainloop_trampoline_ud<T>(ctx: *const c_void, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let flowi = Flowi { api: ctx as _ };
    let f: &&(dyn Fn(&Flowi, &mut T) + 'static) = transmute(wd.func);
    let data = wd.user_data as *mut T;
    f(&flowi, &mut *data);
}

unsafe extern "C" fn mainloop_trampoline(ctx: *const c_void, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let flowi = Flowi { api: ctx as _ };
    let f: &&(dyn Fn(&Flowi) + 'static) = transmute(wd.func);
    f(&flowi);
}

impl Application {
    pub fn main_loop_ud<'a, F, T>(data: &'a mut T, func: F) -> bool
    where
        F: Fn(&Flowi, &mut T) + 'a,
        T: 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&Flowi, &mut T) + 'a>> = Box::new(Box::new(func));
        let user_data = data as *const _ as *const c_void;
        let wrapped_data = WrappedMainData {
            user_data,
            func: Box::into_raw(f) as *const _,
        };

        unsafe {
            fl_application_main_loop_impl(
                transmute(mainloop_trampoline_ud::<T> as usize),
                transmute(&wrapped_data))
        }
    }

    pub fn main_loop<'a, F>(func: F) -> bool
    where
        F: Fn(&Flowi) + 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&Flowi) + 'a>> = Box::new(Box::new(func));
        let wrapped_data = WrappedMainData {
            user_data: ::std::ptr::null(),
            func: Box::into_raw(f) as *const _,
        };

        unsafe {
            fl_application_main_loop_impl(
                transmute(mainloop_trampoline as usize),
                transmute(&wrapped_data))
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
}

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

pub type Result<T> = std::result::Result<T, FlowiError>;
