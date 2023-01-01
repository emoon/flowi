// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[repr(C)]
pub struct FontFfiApi {
    pub(crate) data: *const core::ffi::c_void,
    new_from_file: unsafe extern "C" fn(
        data: *const core::ffi::c_void,
        filename: FlString,
        font_size: u32,
    ) -> u64,
    new_from_memory: unsafe extern "C" fn(
        data: *const core::ffi::c_void,
        name: FlString,
        data: *const u8,
        data_size: u32,
        font_size: u32,
    ) -> u64,
    destroy: unsafe extern "C" fn(data: *const core::ffi::c_void, font: u64),
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct Font {
    pub handle: u64,
}

#[repr(C)]
pub struct FontApi {
    pub api: *const FontFfiApi,
}

impl FontApi {
    /// Create a font from (TTF) file. To use the font use [ui::set_font] before using text-based widgets
    /// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
    pub fn new_from_file(&self, filename: &str, font_size: u32) -> Result<Font> {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.new_from_file)(_api.data, FlString::new(filename), font_size);
            if ret_val == 0 {
                Err(get_last_error())
            } else {
                Ok(Font { handle: ret_val })
            }
        }
    }

    /// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
    /// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
    pub fn new_from_memory(&self, name: &str, data: &[u8], font_size: u32) -> Result<Font> {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.new_from_memory)(
                _api.data,
                FlString::new(name),
                data.as_ptr(),
                data.len() as _,
                font_size,
            );
            if ret_val == 0 {
                Err(get_last_error())
            } else {
                Ok(Font { handle: ret_val })
            }
        }
    }

    /// Destory the current font, render the id invalid
    pub fn destroy(&self, font: Font) {
        unsafe {
            let _api = &*self.api;
            (_api.destroy)(_api.data, font.handle);
        }
    }
}
