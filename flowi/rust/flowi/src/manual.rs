use crate::*;
use flowi_core::Context;
use std::mem::transmute;
use std::os::raw::c_void;

extern "C" {
    fn fl_application_main_loop_impl(callback: *const c_void, user_data: *mut c_void);
}

#[repr(C)]
struct WrappedMainData {
    user_data: *const c_void,
    func: *const c_void,
}

unsafe extern "C" fn mainloop_trampoline_ud<T>(ctx: *const Context, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let context = &*ctx;
    let f: &&(dyn Fn(&Context, &mut T) + 'static) = transmute(wd.func);
    let data = wd.user_data as *mut T;
    f(context, &mut *data);
}

unsafe extern "C" fn mainloop_trampoline(ctx: *const Context, user_data: *mut c_void) {
    let wd: &WrappedMainData = transmute(user_data);
    let context = &*ctx;
    let f: &&(dyn Fn(&Context) + 'static) = transmute(wd.func);
    f(context);
}

impl Application {
    pub fn main_loop_ud<'a, F, T>(data: &'a mut T, func: F)
    where
        F: Fn(&Context, &mut T) + 'a,
        T: 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&Context, &mut T) + 'a>> = Box::new(Box::new(func));
        let user_data = data as *const _ as *const c_void;
        let wrapped_data = WrappedMainData {
            user_data,
            func: Box::into_raw(f) as *const _,
        };

        unsafe {
            fl_application_main_loop_impl(
                transmute(mainloop_trampoline_ud::<T> as usize),
                transmute(&wrapped_data),
            );
        }
    }

    pub fn main_loop<'a, F>(func: F)
    where
        F: Fn(&Context) + 'a,
    {
        // Having the data on the stack is safe as the mainloop only exits after the application is about to end
        let f: Box<Box<dyn Fn(&Context) + 'a>> = Box::new(Box::new(func));
        let wrapped_data = WrappedMainData {
            user_data: ::std::ptr::null(),
            func: Box::into_raw(f) as *const _,
        };

        unsafe {
            fl_application_main_loop_impl(
                transmute(mainloop_trampoline as usize),
                transmute(&wrapped_data),
            );
        }
    }
}
