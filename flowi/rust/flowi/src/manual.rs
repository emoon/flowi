use crate::*;
use std::os::raw::c_void;

pub type MainLoopCallback = extern "C" fn(user_data_0: *mut c_void, user_data_1: *mut c_void);

unsafe extern "C" fn mainloop_trampoline_ud<T>(self_c: *const c_void, func: *const c_void) {
    let f: &&(dyn Fn(&T) + 'static) = std::mem::transmute(func);
    let data = self_c as *const T;
    f(&*data);
}

#[allow(unused_variables)]
unsafe extern "C" fn mainloop_trampoline(self_c: *const c_void, func: *const c_void) {
    let f: &&(dyn Fn() + 'static) = std::mem::transmute(func);
    f();
}

impl Application {
    pub fn main_loop_ud<'a, F, T>(data: &'a T, func: F)
    where
        F: Fn(&T) + 'a,
        T: 'a,
    {
        let f: Box<Box<dyn Fn(&T) + 'a>> = Box::new(Box::new(func));
        let user_data = data as *const _ as *const c_void;

        /*
            ((*funcs).set_pressed_event)(
                obj_data,
                user_data,
                Box::into_raw(f) as *const _,
                transmute(abstract_button_pressed_trampoline_ud::<T> as usize),
            );
        */
    }

    pub fn main_loop<'a, F>(func: F)
    where
        F: Fn() + 'a,
    {
        let f: Box<Box<dyn Fn() + 'a>> = Box::new(Box::new(func));

        /*
            ((*funcs).set_pressed_event)(
                obj_data,
                ::std::ptr::null(),
                Box::into_raw(f) as *const _,
                transmute(abstract_button_pressed_trampoline as usize),
            );
        */
    }
}
