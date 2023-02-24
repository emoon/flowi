// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::manual::{get_last_error, Color, FlString, Result};

#[allow(unused_imports)]
use bitflags::bitflags;

#[allow(unused_imports)]
use crate::math_data::*;

#[allow(unused_imports)]
use crate::window::*;

#[repr(C)]
pub struct ItemFfiApi {
    pub(crate) data: *const core::ffi::c_void,
    is_hovered: unsafe extern "C" fn(data: *const core::ffi::c_void, flags: HoveredFlags) -> bool,
    is_active: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_focused: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_clicked: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_visible: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_edited: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_activated: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_deactivated: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_deactivated_after_edit: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_toggled_open: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_any_hovered: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_any_active: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    is_any_focused: unsafe extern "C" fn(data: *const core::ffi::c_void) -> bool,
    get_rect_min: unsafe extern "C" fn(data: *const core::ffi::c_void) -> Vec2,
    get_rect_max: unsafe extern "C" fn(data: *const core::ffi::c_void) -> Vec2,
    get_rect_size: unsafe extern "C" fn(data: *const core::ffi::c_void) -> Vec2,
    set_allow_overlap: unsafe extern "C" fn(data: *const core::ffi::c_void),
}

#[repr(C)]
#[derive(Debug)]
pub struct Item {
    _dummy: u32,
}

#[repr(C)]
pub struct ItemApi {
    pub api: *const ItemFfiApi,
}

impl ItemApi {
    /// Is the last item hovered? (and usable, aka not blocked by a popup, etc.). See ImGuiHoveredFlags for more options.
    pub fn is_hovered(&self, flags: HoveredFlags) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_hovered)(_api.data, flags);
            ret_val
        }
    }

    /// Is the last item active? (e.g. button being held, text field being edited. This will continuously return true while holding mouse button on an item. _s that don't interact will always return false)
    pub fn is_active(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_active)(_api.data);
            ret_val
        }
    }

    /// Is the last item focused for keyboard/gamepad navigation?
    pub fn is_focused(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_focused)(_api.data);
            ret_val
        }
    }

    /// Is the last item hovered and mouse clicked on? (**)  == IsMouseClicked(mouse_button) && Is_Hovered()Important. (**) this is NOT equivalent to the behavior of e.g. Button(). Read comments in function definition.
    pub fn is_clicked(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_clicked)(_api.data);
            ret_val
        }
    }

    /// Is the last item visible? (items may be out of sight because of clipping/scrolling)
    pub fn is_visible(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_visible)(_api.data);
            ret_val
        }
    }

    /// Did the last item modify its underlying value this frame? or was pressed? This is generally the same as the "bool" return value of many widgets.
    pub fn is_edited(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_edited)(_api.data);
            ret_val
        }
    }

    /// Was the last item just made active (item was previously inactive).
    pub fn is_activated(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_activated)(_api.data);
            ret_val
        }
    }

    /// Was the last item just made inactive (item was previously active). Useful for Undo/Redo patterns with widgets that require continuous editing.
    pub fn is_deactivated(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_deactivated)(_api.data);
            ret_val
        }
    }

    /// Was the last item just made inactive and made a value change when it was active? (e.g. Slider/Drag moved). Useful for Undo/Redo patterns with widgets that require continuous editing. Note that you may get false positives (some widgets such as Combo()/ListBox()/Selectable() will return true even when clicking an already selected item).
    pub fn is_deactivated_after_edit(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_deactivated_after_edit)(_api.data);
            ret_val
        }
    }

    /// Was the last item open state toggled? set by TreeNode().
    pub fn is_toggled_open(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_toggled_open)(_api.data);
            ret_val
        }
    }

    /// Is any item hovered?
    pub fn is_any_hovered(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_any_hovered)(_api.data);
            ret_val
        }
    }

    /// Is any item active?
    pub fn is_any_active(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_any_active)(_api.data);
            ret_val
        }
    }

    /// Is any item focused?
    pub fn is_any_focused(&self) -> bool {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.is_any_focused)(_api.data);
            ret_val
        }
    }

    /// Get upper-left bounding rectangle of the last item (screen space)
    pub fn get_rect_min(&self) -> Vec2 {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.get_rect_min)(_api.data);
            ret_val
        }
    }

    /// Get lower-right bounding rectangle of the last item (screen space)
    pub fn get_rect_max(&self) -> Vec2 {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.get_rect_max)(_api.data);
            ret_val
        }
    }

    /// Get size of last item
    pub fn get_rect_size(&self) -> Vec2 {
        unsafe {
            let _api = &*self.api;
            let ret_val = (_api.get_rect_size)(_api.data);
            ret_val
        }
    }

    /// Allow last item to be overlapped by a subsequent item. sometimes useful with invisible buttons, selectables, etc. to catch unused area.
    pub fn set_allow_overlap(&self) {
        unsafe {
            let _api = &*self.api;
            (_api.set_allow_overlap)(_api.data);
        }
    }
}