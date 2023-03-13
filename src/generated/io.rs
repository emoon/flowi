// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[allow(unused_imports)]
use crate::image::*;

#[allow(unused_imports)]
use crate::shader::*;

#[repr(C)]
pub struct IoFfiApi {
    pub(crate) data: *const core::ffi::c_void,
    pub(crate) load_shader_program_comp: unsafe extern "C" fn(
        data: *const core::ffi::c_void,
        vs_filename: FlString,
        ps_filename: FlString,
    ) -> u64,
}

#[repr(C)]
#[derive(Debug)]
pub struct Io {
    _dummy: u32,
}

#[repr(C)]
pub struct IoApi {
    pub api: *const IoFfiApi,
}

impl IoApi {
    /// Load image from file/url. Supported formats are:
    /// JPEG baseline & progressive (12 bpc/arithmetic not supported, same as stock IJG lib)
    /// PNG 1/2/4/8/16-bit-per-channel
    /// TGA
    /// BMP non-1bpp, non-RLE
    /// PSD (composited view only, no extra channels, 8/16 bit-per-channel)
    /// GIF
    /// HDR (radiance rgbE format)
    /// PIC (Softimage PIC)
    /// PNM (PPM and PGM binary only)
    /// Same as load_image_from_url, but async and gives back a handle to check/access data later.
    /// Load a vertex shader be used for rendering. This will also compile the shader.
    /// Load a pixel shader to be used for rendering. This will also compile the shader.
    /// Load a vertex shader and pixel shader to be used as a shader program. This will also compile the shaders.
    pub fn load_shader_program_comp(
        &self,
        vs_filename: &str,
        ps_filename: &str,
    ) -> Result<ShaderProgram> {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.load_shader_program_comp)(
                _api.data,
                FlString::new(vs_filename),
                FlString::new(ps_filename),
            );
            if ret_val == 0 {
                Err(get_last_error())
            } else {
                Ok(ShaderProgram { handle: ret_val })
            }
        }
    }
}
