// This file is auto-generated by api_gen. DO NOT EDIT!

use core::ffi::c_void;
pub mod application;
pub mod context;
pub mod debug;
pub mod error;
pub mod font;
pub mod image;
pub mod layout;
pub mod manual;
pub mod math_data;
pub mod render_commands;
pub mod style;
pub mod ui;
pub mod window;
pub use manual::*;

pub use crate::font::FontApi;
use crate::font::FontFfiApi;
pub use crate::image::ImageApi;
use crate::image::ImageFfiApi;
pub use crate::style::StyleApi;
use crate::style::StyleFfiApi;
pub use crate::ui::UiApi;
use crate::ui::UiFfiApi;

#[repr(C)]
pub struct FlowiFfiApi {
    data: *const c_void,
    font_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const FontFfiApi,
    image_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const ImageFfiApi,
    style_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const StyleFfiApi,
    ui_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const UiFfiApi,
}

#[repr(C)]
pub struct Flowi {
    api: *const FlowiFfiApi,
}

impl Flowi {
    pub fn font(&self) -> FontApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.font_get_api)(api_priv.data, 0) };
        FontApi { api }
    }

    pub fn image(&self) -> ImageApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.image_get_api)(api_priv.data, 0) };
        ImageApi { api }
    }

    pub fn style(&self) -> StyleApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.style_get_api)(api_priv.data, 0) };
        StyleApi { api }
    }

    pub fn ui(&self) -> UiApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.ui_get_api)(api_priv.data, 0) };
        UiApi { api }
    }
}
