// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

extern "C" {
    fn fl_font_new_from_file_impl(
        ctx: *const core::ffi::c_void,
        filename: FlString,
        font_size: u32,
        placement_mode: FontPlacementMode,
    ) -> u64;
    fn fl_font_new_from_memory_impl(
        ctx: *const core::ffi::c_void,
        name: FlString,
        data: *const u8,
        data_size: u32,
        font_size: u32,
        placement_mode: FontPlacementMode,
    ) -> u64;
    fn fl_font_set_impl(ctx: *const core::ffi::c_void, font: u64);
    fn fl_font_set_with_size_impl(ctx: *const core::ffi::c_void, size: u32);
    fn fl_font_destroy_impl(ctx: *const core::ffi::c_void, font: u64);
}

/// Allows the user to select how accurate the glyph placement should be.
/// The list has a the fastest (CPU performance wise) first (Monospace) and the slowest (Accurate) last
/// Rule of thumb is:
/// Auto (Same as basic)
/// Monospaced (code/fixed size fonts) - use Monospace mode
/// Regular Latin text - use Basic mode
/// Hebrew and other complex languages that require accurate layout - Use accurate
#[repr(C)]
#[derive(Debug)]
pub enum FontPlacementMode {
    /// Let the library decide the mode (default)
    Auto = 0,
    /// Used for regular Latin based text
    Basic = 1,
    /// Used for fixed-width monospaces fonts (Fastest)
    Mono = 2,
    /// Used for accurate glyph placement (uses the Harfbuzz lib thus is the slowest mode)
    Accurate = 3,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct Font {
    pub handle: u64,
}

impl Context {
    /// Create a font from (TTF) file. To use the font use [Font::set] or [Font::set_with_size] before using text-based widgets
    /// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
    pub fn font_new_from_file(
        &self,
        filename: &str,
        font_size: u32,
        placement_mode: FontPlacementMode,
    ) -> Result<Font> {
        unsafe {
            let self_ = std::mem::transmute(self);
            let ret_val = fl_font_new_from_file_impl(
                self_,
                FlString::new(filename),
                font_size,
                placement_mode,
            );
            if ret_val == 0 {
                Err(get_last_error())
            } else {
                Ok(Font { handle: ret_val })
            }
        }
    }

    /// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
    /// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
    pub fn font_new_from_memory(
        &self,
        name: &str,
        data: &[u8],
        font_size: u32,
        placement_mode: FontPlacementMode,
    ) -> Result<Font> {
        unsafe {
            let self_ = std::mem::transmute(self);
            let ret_val = fl_font_new_from_memory_impl(
                self_,
                FlString::new(name),
                data.as_ptr(),
                data.len() as _,
                font_size,
                placement_mode,
            );
            if ret_val == 0 {
                Err(get_last_error())
            } else {
                Ok(Font { handle: ret_val })
            }
        }
    }

    /// Set the font as active when drawing text
    pub fn font_set(&self, font: Font) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_font_set_impl(self_, font.handle);
        }
    }

    /// Set font active with specific size in pixels
    pub fn font_set_with_size(&self, size: u32) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_font_set_with_size_impl(self_, size);
        }
    }

    /// Destory the current font, render the id invalid
    pub fn font_destroy(&self, font: Font) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_font_destroy_impl(self_, font.handle);
        }
    }
}
