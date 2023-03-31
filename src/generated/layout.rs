// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[allow(unused_imports)]
use crate::math_data::*;

#[repr(C)]
pub struct CursorFfiApi {
    pub(crate) data: *const core::ffi::c_void,
    pub(crate) separator: unsafe extern "C" fn(data: *const core::ffi::c_void),
    pub(crate) same_line: unsafe extern "C" fn(
        data: *const core::ffi::c_void,
        offset_from_start_x: f32,
        spacing: f32,
    ),
    pub(crate) new_line: unsafe extern "C" fn(data: *const core::ffi::c_void),
    pub(crate) spacing: unsafe extern "C" fn(data: *const core::ffi::c_void),
    pub(crate) dummy: unsafe extern "C" fn(data: *const core::ffi::c_void, size: Vec2),
    pub(crate) indent: unsafe extern "C" fn(data: *const core::ffi::c_void, indent: f32),
    pub(crate) unindent: unsafe extern "C" fn(data: *const core::ffi::c_void, indent_w: f32),
    pub(crate) begin_group: unsafe extern "C" fn(data: *const core::ffi::c_void),
    pub(crate) end_group: unsafe extern "C" fn(data: *const core::ffi::c_void),
    pub(crate) get_pos: unsafe extern "C" fn(data: *const core::ffi::c_void) -> Vec2,
    pub(crate) get_pos_x: unsafe extern "C" fn(data: *const core::ffi::c_void) -> f32,
    pub(crate) get_pos_y: unsafe extern "C" fn(data: *const core::ffi::c_void) -> f32,
    pub(crate) set_pos: unsafe extern "C" fn(data: *const core::ffi::c_void, pos: Vec2),
    pub(crate) set_pos_x: unsafe extern "C" fn(data: *const core::ffi::c_void, x: f32),
    pub(crate) set_pos_y: unsafe extern "C" fn(data: *const core::ffi::c_void, y: f32),
    pub(crate) screen_pos: unsafe extern "C" fn(data: *const core::ffi::c_void) -> Vec2,
    pub(crate) set_screen_pos: unsafe extern "C" fn(data: *const core::ffi::c_void, pos: Vec2),
    pub(crate) align_text_to_frame_padding: unsafe extern "C" fn(data: *const core::ffi::c_void),
    pub(crate) get_text_line_height: unsafe extern "C" fn(data: *const core::ffi::c_void) -> f32,
    pub(crate) get_text_line_height_with_spacing:
        unsafe extern "C" fn(data: *const core::ffi::c_void) -> f32,
    pub(crate) get_frame_height: unsafe extern "C" fn(data: *const core::ffi::c_void) -> f32,
    pub(crate) get_frame_height_with_spacing:
        unsafe extern "C" fn(data: *const core::ffi::c_void) -> f32,
}

#[cfg(any(feature = "static", feature = "tundra"))]
extern "C" {
    fn fl_cursor_separator_impl(data: *const core::ffi::c_void);
    fn fl_cursor_same_line_impl(
        data: *const core::ffi::c_void,
        offset_from_start_x: f32,
        spacing: f32,
    );
    fn fl_cursor_new_line_impl(data: *const core::ffi::c_void);
    fn fl_cursor_spacing_impl(data: *const core::ffi::c_void);
    fn fl_cursor_dummy_impl(data: *const core::ffi::c_void, size: Vec2);
    fn fl_cursor_indent_impl(data: *const core::ffi::c_void, indent: f32);
    fn fl_cursor_unindent_impl(data: *const core::ffi::c_void, indent_w: f32);
    fn fl_cursor_begin_group_impl(data: *const core::ffi::c_void);
    fn fl_cursor_end_group_impl(data: *const core::ffi::c_void);
    fn fl_cursor_get_pos_impl(data: *const core::ffi::c_void) -> Vec2;
    fn fl_cursor_get_pos_x_impl(data: *const core::ffi::c_void) -> f32;
    fn fl_cursor_get_pos_y_impl(data: *const core::ffi::c_void) -> f32;
    fn fl_cursor_set_pos_impl(data: *const core::ffi::c_void, pos: Vec2);
    fn fl_cursor_set_pos_x_impl(data: *const core::ffi::c_void, x: f32);
    fn fl_cursor_set_pos_y_impl(data: *const core::ffi::c_void, y: f32);
    fn fl_cursor_screen_pos_impl(data: *const core::ffi::c_void) -> Vec2;
    fn fl_cursor_set_screen_pos_impl(data: *const core::ffi::c_void, pos: Vec2);
    fn fl_cursor_align_text_to_frame_padding_impl(data: *const core::ffi::c_void);
    fn fl_cursor_get_text_line_height_impl(data: *const core::ffi::c_void) -> f32;
    fn fl_cursor_get_text_line_height_with_spacing_impl(data: *const core::ffi::c_void) -> f32;
    fn fl_cursor_get_frame_height_impl(data: *const core::ffi::c_void) -> f32;
    fn fl_cursor_get_frame_height_with_spacing_impl(data: *const core::ffi::c_void) -> f32;
}

#[no_mangle]
pub static mut g_flowi_cursor_api: *const CursorFfiApi = std::ptr::null_mut();

/// Layout Cursor
/// Cursor means the position of the widget.
/// By setting the cursor position, you can change the position of the widget.
/// You can call same_line() between widgets to undo the last carriage return and output at the right of the preceding widget.
#[repr(C)]
#[derive(Debug)]
pub struct Cursor {
    _dummy: u32,
}

impl Cursor {
    /// Separator, generally horizontal. Inside a menu bar or in horizontal layout mode, this becomes a vertical separator.
    pub fn separator() {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.separator)(_api.data);
        }
    }

    /// Call between widgets or groups to layout them horizontally. X position given in window coordinates.
    pub fn same_line(offset_from_start_x: f32, spacing: f32) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.same_line)(_api.data, offset_from_start_x, spacing);
        }
    }

    /// Undo a same_line() or force a new line when in a horizontal-layout context.
    pub fn new_line() {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.new_line)(_api.data);
        }
    }

    /// Undo a same_line() or force a new line when in a horizontal-layout context.
    pub fn spacing() {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.spacing)(_api.data);
        }
    }

    /// Add a dummy item of given size. Unlike widgets.invisible_button(), dummmy() won't take the mouse click or be navigable into.
    pub fn dummy(size: Vec2) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.dummy)(_api.data, size);
        }
    }

    /// Move content position toward the right, by indent_w, or style.IndentSpacing if indent_w <= 0
    pub fn indent(indent: f32) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.indent)(_api.data, indent);
        }
    }

    /// Move content position back to the left, by indent_w, or style.IndentSpacing if indent_w <= 0
    pub fn unindent(indent_w: f32) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.unindent)(_api.data, indent_w);
        }
    }

    pub fn begin_group() {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.begin_group)(_api.data);
        }
    }

    pub fn end_group() {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.end_group)(_api.data);
        }
    }

    /// Cursor position in window coordinates (relative to window position)
    pub fn get_pos() -> Vec2 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_pos_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_pos)(_api.data);
            ret_val
        }
    }

    pub fn get_pos_x() -> f32 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_pos_x_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_pos_x)(_api.data);
            ret_val
        }
    }

    pub fn get_pos_y() -> f32 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_pos_y_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_pos_y)(_api.data);
            ret_val
        }
    }

    /// Set position in window coordinates (relative to window position)
    pub fn set_pos(pos: Vec2) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.set_pos)(_api.data, pos);
        }
    }

    pub fn set_pos_x(x: f32) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.set_pos_x)(_api.data, x);
        }
    }

    pub fn set_pos_y(y: f32) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.set_pos_y)(_api.data, y);
        }
    }

    /// cursor position in absolute coordinates (useful to work with ImDrawList API).
    /// generally top-left == GetMainViewport()->Pos == (0,0) in single viewport mode,
    /// and bottom-right == GetMainViewport()->Pos+Size == io.DisplaySize in single-viewport mode.
    pub fn screen_pos() -> Vec2 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_screen_pos_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.screen_pos)(_api.data);
            ret_val
        }
    }

    pub fn set_screen_pos(pos: Vec2) {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.set_screen_pos)(_api.data, pos);
        }
    }

    /// vertically align upcoming text baseline to FramePadding.y so that it will align properly to regularly framed items (call if you have text on a line before a framed item)
    pub fn align_text_to_frame_padding() {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            (_api.align_text_to_frame_padding)(_api.data);
        }
    }

    /// ~ FontSize
    pub fn get_text_line_height() -> f32 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_text_line_height_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_text_line_height)(_api.data);
            ret_val
        }
    }

    /// ~ FontSize + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of text)
    pub fn get_text_line_height_with_spacing() -> f32 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_text_line_height_with_spacing_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_text_line_height_with_spacing)(_api.data);
            ret_val
        }
    }

    /// ~ FontSize + style.FramePadding.y * 2
    pub fn get_frame_height() -> f32 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_frame_height_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_frame_height)(_api.data);
            ret_val
        }
    }

    /// ~ FontSize + style.FramePadding.y * 2 + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of framed widgets)
    pub fn get_frame_height_with_spacing() -> f32 {
        unsafe {
            let _api = &*g_flowi_cursor_api;
            #[cfg(any(feature = "static", feature = "tundra"))]
            let ret_val = fl_cursor_get_frame_height_with_spacing_impl(_api.data);
            #[cfg(any(feature = "dynamic", feature = "plugin"))]
            let ret_val = (_api.get_frame_height_with_spacing)(_api.data);
            ret_val
        }
    }
}
