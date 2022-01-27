// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

extern "C" {
    fn fl_style_create(name: FlString) -> *mut Style;
    fn fl_style_get_default() -> *mut Style;
    fn fl_style_get_current() -> *const Style;
    fn fl_style_end_changes(self_c: *mut Style);
    fn fl_style_push(self_c: *mut Style);
    fn fl_style_pop(self_c: *mut Style);
}

#[repr(C)]
pub enum LengthPercent {
    Length = 0,
    Percent = 1,
}

#[repr(C)]
pub struct LengthPercentValue {
    value: f32,
    typ: LengthPercent,
}

#[repr(C)]
pub struct Spacing {
    top: u16,
    right: u16,
    bottom: u16,
    left: u16,
}

#[repr(C)]
pub struct Padding {
    top: u16,
    right: u16,
    bottom: u16,
    left: u16,
}

#[repr(C)]
pub struct Border {
    border_radius_top: LengthPercentValue,
    border_radius_right: LengthPercentValue,
    border_radius_bottom: LengthPercentValue,
    border_radius_left: LengthPercentValue,
}

#[repr(C)]
pub struct Style {
    name: FlString,
    border: Border,
    padding: Padding,
    current_font: u32,
    background_color: Color,
    text_color: Color,
    font_color: Color,
}

impl LengthPercentValue {}

impl Spacing {}

impl Padding {}

impl Border {}

impl Style {
    /// Create a new style
    pub fn create(name: &str) {
        unsafe {
            fl_style_create(FlString::new(name));
        }
    }

    /// Get the default style. Changing this will apply the base style for the whole application
    pub fn get_default() {
        unsafe {
            fl_style_get_default();
        }
    }

    /// Get the current style which is based on what has been pushed on the style stack using push/pop
    pub fn get_current() {
        unsafe {
            fl_style_get_current();
        }
    }

    /// Mark the end of style changes
    pub fn end_changes(&self) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_style_end_changes(self_);
        }
    }

    /// Select the style to be used, to end using the style use 'fl_pop_style()'
    pub fn push(&self) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_style_push(self_);
        }
    }

    /// Pops the current style
    pub fn pop(&self) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_style_pop(self_);
        }
    }
}
