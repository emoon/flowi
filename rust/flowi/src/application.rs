// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[allow(dead_code)]
type MainLoopCallback =
    extern "C" fn(data: *const core::ffi::c_void, user_data: *mut core::ffi::c_void);

#[repr(C)]
#[derive(Debug)]
pub struct Application {
    _dummy: u32,
}
