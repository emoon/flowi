// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[allow(unused_imports)]
use crate::math_data::*;

#[repr(C)]
pub struct TextFfiApi {
    pub(crate) data: *const core::ffi::c_void,
    bullet: unsafe extern "C" fn(data: *const core::ffi::c_void, text: FlString),
    label: unsafe extern "C" fn(data: *const core::ffi::c_void, label: FlString, text: FlString),
    show_color: unsafe extern "C" fn(data: *const core::ffi::c_void, color: Color, text: FlString),
    show: unsafe extern "C" fn(data: *const core::ffi::c_void, text: FlString),
    text_disabled: unsafe extern "C" fn(data: *const core::ffi::c_void, text: FlString),
}

#[repr(C)]
#[derive(Debug)]
pub struct Text {
    _dummy: u32,
}

#[repr(C)]
pub struct TextApi {
    pub api: *const TextFfiApi,
}

impl TextApi {
    pub fn bullet(&self, text: &str) {
        unsafe {
            let _api = &*self.api;
            (_api.bullet)(_api.data, FlString::new(text));
        }
    }

    pub fn label(&self, label: &str, text: &str) {
        unsafe {
            let _api = &*self.api;
            (_api.label)(_api.data, FlString::new(label), FlString::new(text));
        }
    }

    pub fn show_color(&self, color: Color, text: &str) {
        unsafe {
            let _api = &*self.api;
            (_api.show_color)(_api.data, color, FlString::new(text));
        }
    }

    pub fn show(&self, text: &str) {
        unsafe {
            let _api = &*self.api;
            (_api.show)(_api.data, FlString::new(text));
        }
    }

    pub fn text_disabled(&self, text: &str) {
        unsafe {
            let _api = &*self.api;
            (_api.text_disabled)(_api.data, FlString::new(text));
        }
    }
}