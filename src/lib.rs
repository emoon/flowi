// This file is auto-generated by api_gen. DO NOT EDIT!

use core::ffi::c_void;
pub mod application_settings;
pub use application_settings::*;
pub mod button;
pub use button::*;
pub mod context;
pub use context::*;
pub mod debug;
pub use debug::*;
pub mod error;
pub use error::*;
pub mod font;
pub use font::*;
pub mod image;
pub use image::*;
pub mod io;
pub use io::*;
pub mod item;
pub use item::*;
pub mod layout;
pub use layout::*;
pub mod math_data;
pub use math_data::*;
pub mod menu;
pub use menu::*;
pub mod painter;
pub use painter::*;
pub mod render_commands;
pub use render_commands::*;
pub mod shader;
pub use shader::*;
pub mod style;
pub use style::*;
pub mod text;
pub use text::*;
pub mod ui;
pub use ui::*;
pub mod window;
pub use window::*;
pub mod manual;
pub use manual::*;

pub mod io_handler;
pub use io_handler::*;

pub use crate::button::ButtonApi;
use crate::button::ButtonFfiApi;
pub use crate::font::FontApi;
use crate::font::FontFfiApi;
pub use crate::image::ImageApi;
use crate::image::ImageFfiApi;
pub use crate::io::IoApi;
use crate::io::IoFfiApi;
pub use crate::item::ItemApi;
use crate::item::ItemFfiApi;
pub use crate::layout::CursorApi;
use crate::layout::CursorFfiApi;
pub use crate::menu::MenuApi;
use crate::menu::MenuFfiApi;
pub use crate::painter::PainterApi;
use crate::painter::PainterFfiApi;
pub use crate::style::StyleApi;
use crate::style::StyleFfiApi;
pub use crate::text::TextApi;
use crate::text::TextFfiApi;
pub use crate::ui::UiApi;
use crate::ui::UiFfiApi;
pub use crate::window::WindowApi;
use crate::window::WindowFfiApi;

#[repr(C)]
pub struct FlowiFfiApi {
    data: *const c_void,
    button_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const ButtonFfiApi,
    cursor_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const CursorFfiApi,
    font_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const FontFfiApi,
    image_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const ImageFfiApi,
    io_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const IoFfiApi,
    item_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const ItemFfiApi,
    menu_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const MenuFfiApi,
    painter_get_api:
        unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const PainterFfiApi,
    style_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const StyleFfiApi,
    text_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const TextFfiApi,
    ui_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const UiFfiApi,
    window_get_api: unsafe extern "C" fn(data: *const c_void, api_ver: u32) -> *const WindowFfiApi,
}

#[repr(C)]
pub struct Flowi {
    api: *const FlowiFfiApi,
}

impl Flowi {
    pub fn button(&self) -> ButtonApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.button_get_api)(api_priv.data, 0) };
        ButtonApi { api }
    }

    pub fn cursor(&self) -> CursorApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.cursor_get_api)(api_priv.data, 0) };
        CursorApi { api }
    }

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

    pub fn io(&self) -> IoApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.io_get_api)(api_priv.data, 0) };
        IoApi { api }
    }

    pub fn item(&self) -> ItemApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.item_get_api)(api_priv.data, 0) };
        ItemApi { api }
    }

    pub fn menu(&self) -> MenuApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.menu_get_api)(api_priv.data, 0) };
        MenuApi { api }
    }

    pub fn painter(&self) -> PainterApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.painter_get_api)(api_priv.data, 0) };
        PainterApi { api }
    }

    pub fn style(&self) -> StyleApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.style_get_api)(api_priv.data, 0) };
        StyleApi { api }
    }

    pub fn text(&self) -> TextApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.text_get_api)(api_priv.data, 0) };
        TextApi { api }
    }

    pub fn ui(&self) -> UiApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.ui_get_api)(api_priv.data, 0) };
        UiApi { api }
    }

    pub fn window(&self) -> WindowApi {
        let api_priv = unsafe { &*self.api };
        let api = unsafe { (api_priv.window_get_api)(api_priv.data, 0) };
        WindowApi { api }
    }
}
