// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

#[allow(unused_imports)]
use flowi_core::*;

extern "C" {
    fn fl_application_create(application_name: FlString, developer: FlString) -> *const Context;
}

#[allow(dead_code)]
type MainLoopCallback =
    extern "C" fn(ctx: *const core::ffi::c_void, user_data: *mut core::ffi::c_void);

#[repr(C)]
#[derive(Debug)]
pub struct Application {
    _dummy: u32,
}

impl Application {
    /// TODO: More options
    pub fn create<'a>(application_name: &str, developer: &str) -> Result<&'a Context> {
        unsafe {
            let ret_val =
                fl_application_create(FlString::new(application_name), FlString::new(developer));
            if ret_val.is_null() {
                Err(get_last_error())
            } else {
                Ok(&*ret_val)
            }
        }
    }
}