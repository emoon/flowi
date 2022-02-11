// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

#[allow(unused_imports)]
use flowi_core::*;

extern "C" {
    fn fl_window_new_impl(ctx: *const core::ffi::c_void, width: u16, height: u16) -> Window;
    fn fl_window_destroy_impl(self_c: *mut Window);
    fn fl_window_is_open_impl(self_c: *mut Window) -> bool;
    fn fl_window_update_impl(self_c: *mut Window);
}

#[repr(C)]
pub struct Window {
    handle: u64,
}

impl Ui {
    /// Opens up new window
    pub fn window_new(&self, width: u16, height: u16) -> Window {
        unsafe {
            let ret_val = fl_window_new_impl(self.ctx, width, height);
            ret_val
        }
    }
}

impl Window {
    /// Destroy the window
    pub fn destroy(&self) {
        unsafe {
            fl_window_destroy_impl(self.handle);
        }
    }

    /// Check if the current window is still open
    pub fn is_open(&self) -> bool {
        unsafe {
            let ret_val = fl_window_is_open_impl(self.handle);
            ret_val
        }
    }

    /// Update the window. This has to be done in a loop for the UI to fuction correctly
    pub fn update(&self) {
        unsafe {
            fl_window_update_impl(self.handle);
        }
    }
}
