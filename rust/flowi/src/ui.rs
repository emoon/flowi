// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[allow(unused_imports)]
use crate::layout::*;

#[allow(unused_imports)]
use crate::math_data::*;

#[allow(unused_imports)]
use crate::image::*;

#[repr(C)]
pub struct UiFfiApi {
    pub(crate) data: *const core::ffi::c_void,
    window_begin: unsafe extern "C" fn(
        data: *const core::ffi::c_void,
        name: FlString,
        flags: WindowFlags,
    ) -> bool,
    end: unsafe extern "C" fn(data: *const core::ffi::c_void),
    text: unsafe extern "C" fn(data: *const core::ffi::c_void, text: FlString),
    image: unsafe extern "C" fn(data: *const core::ffi::c_void, image: Image),
    image_with_size: unsafe extern "C" fn(data: *const core::ffi::c_void, image: Image, size: Vec2),
    set_pos: unsafe extern "C" fn(data: *const core::ffi::c_void, pos: Vec2),
    get_last_widget_size: unsafe extern "C" fn(data: *const core::ffi::c_void, pos: Vec2) -> Rect,
    push_button_with_icon: unsafe extern "C" fn(
        data: *const core::ffi::c_void,
        text: FlString,
        image: Image,
        text_pos: Vec2,
        image_scale: f32,
    ) -> bool,
    push_button: unsafe extern "C" fn(data: *const core::ffi::c_void, text: FlString) -> bool,
}

bitflags! {
 pub struct WindowFlags : u32 {
    /// Default flags
    const NONE = 0;
    /// Disable title-bar
    const NO_TITLE_BAR = 1 << 0;
    /// Disable user resizing with the lower-right grip
    const NO_RESIZE = 1 << 1;
    /// Disable user moving the window
    const NO_MOVE = 1 << 2;
    /// Disable scrollbars (window can still scroll with mouse or programmatically)
    const NO_SCROLLBAR = 1 << 3;
    /// Disable user vertically scrolling with mouse wheel. On child window, mouse wheel will be forwarded to the parent unless NoScrollbar is also set.
    const NO_SCROLL_WITH_MOUSE = 1 << 4;
    /// Disable user collapsing window by double-clicking on it. Also referred to as Window Menu Button (e.g. within a docking node).
    const NO_COLLAPSE = 1 << 5;
    /// Resize every window to its content every frame
    const ALWAYS_AUTO_RESIZE = 1 << 6;
    /// Disable drawing background color (WindowBg, etc.) and outside border. Similar as using SetNextWindowBgAlpha(0.0f).
    const NO_BACKGROUND = 1 << 7;
    /// Never load/save settings in .ini file
    const NO_SAVED_SETTINGS = 1 << 8;
    /// Disable catching mouse, hovering test with pass through.
    const NO_MOUSE_INPUTS = 1 << 9;
    /// Has a menu-bar
    const MENU_BAR = 1 << 10;
    /// Allow horizontal scrollbar to appear (off by default).
    const HORIZONTAL_SCROLLBAR = 1 << 11;
    /// Disable taking focus when transitioning from hidden to visible state
    const NO_FOCUS_ON_APPEARING = 1 << 12;
    /// Disable bringing window to front when taking focus (e.g. clicking on it or programmatically giving it focus)
    const NO_BRING_TO_FRONT_ON_FOCUS = 1 << 13;
    /// Always show vertical scrollbar (even if content_size.y < size.y)
    const ALWAYS_VERTICAL_SCROLLBAR = 1 << 14;
    /// Always show horizontal scrollbar (even if content_size.x < size.x)
    const ALWAYS_HORIZONTAL_SCROLLBAR = 1 << 15;
    /// Ensure child windows without border uses style.WindowPadding (ignored by default for non-bordered child windows,
    const ALWAYS_USE_WINDOW_PADDING = 1 << 16;
    /// No gamepad/keyboard navigation within the window
    const NO_NAV_INPUTS = 1 << 17;
    /// No focusing toward this window with gamepad/keyboard navigation (e.g. skipped by CTRL+TAB)
    const NO_NAV_FOCUS = 1 << 18;
    /// Display a dot next to the title. When used in a tab/docking context, tab is selected when clicking the X +
    /// closure is not assumed (will wait for user to stop submitting the tab). Otherwise closure is assumed when
    /// pressing the X, so if you keep submitting the tab may reappear at end of tab bar.
    const UNSAVED_DOCUMENT = 1 << 19;
    const NO_NAV = Self::NO_NAV_INPUTS.bits() | Self::NO_NAV_FOCUS.bits();
    const NO_DECORATION = Self::NO_TITLE_BAR.bits() | Self::NO_RESIZE.bits() | Self::NO_SCROLLBAR.bits() | Self::NO_COLLAPSE.bits();
    const NO_INPUTS = Self::NO_MOUSE_INPUTS.bits() | Self::NO_NAV_INPUTS.bits() | Self::NO_NAV_FOCUS.bits();
}}

#[repr(C)]
#[derive(Debug)]
pub struct Ui {
    _dummy: u32,
}

#[repr(C)]
pub struct UiApi {
    pub api: *const UiFfiApi,
}

impl UiApi {
    /// Start a window
    pub fn window_begin(&self, name: &str, flags: WindowFlags) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.window_begin)(_api.data, FlString::new(name), flags);
            ret_val
        }
    }

    /// End call for various types such as windows, lists, etc.
    pub fn end(&self) {
        unsafe {
            let _api = &*self.api;
            (_api.end)(_api.data);
        }
    }

    /// Draw static text with the selected font
    pub fn text(&self, text: &str) {
        unsafe {
            let _api = &*self.api;
            (_api.text)(_api.data, FlString::new(text));
        }
    }

    /// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
    pub fn image(&self, image: Image) {
        unsafe {
            let _api = &*self.api;
            (_api.image)(_api.data, image);
        }
    }

    /// Draw image with given size
    pub fn image_with_size(&self, image: Image, size: Vec2) {
        unsafe {
            let _api = &*self.api;
            (_api.image_with_size)(_api.data, image, size);
        }
    }

    /// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
    pub fn set_pos(&self, pos: Vec2) {
        unsafe {
            let _api = &*self.api;
            (_api.set_pos)(_api.data, pos);
        }
    }

    /// Get the last widget size. This is usually used for doing manual layouting
    pub fn get_last_widget_size(&self, pos: Vec2) -> Rect {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.get_last_widget_size)(_api.data, pos);
            ret_val
        }
    }

    /// Push button widget that returns true if user has pressed it
    pub fn push_button_with_icon(
        &self,
        text: &str,
        image: Image,
        text_pos: Vec2,
        image_scale: f32,
    ) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.push_button_with_icon)(
                _api.data,
                FlString::new(text),
                image,
                text_pos,
                image_scale,
            );
            ret_val
        }
    }

    /// Push button widget that returns true if user has pressed it
    pub fn push_button(&self, text: &str) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.push_button)(_api.data, FlString::new(text));
            ret_val
        }
    }
}
