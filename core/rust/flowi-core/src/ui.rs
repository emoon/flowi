// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

extern "C" {
    fn fl_ui_set_layer_impl(ctx: *const core::ffi::c_void, layer: LayerType);
    fn fl_ui_text_impl(ctx: *const core::ffi::c_void, text: FlString);
    fn fl_ui_image_impl(ctx: *const core::ffi::c_void, image: Image);
    fn fl_ui_image_with_size_impl(ctx: *const core::ffi::c_void, image: Image, size: Vec2);
    fn fl_ui_set_pos_impl(ctx: *const core::ffi::c_void, pos: Vec2);
    fn fl_ui_get_last_widget_size_impl(ctx: *const core::ffi::c_void, pos: Vec2) -> Rect;
    fn fl_ui_push_button_with_icon_impl(
        ctx: *const core::ffi::c_void,
        text: FlString,
        image: Image,
        text_pos: Vec2,
        image_scale: f32,
    ) -> bool;
    fn fl_ui_push_button_impl(ctx: *const core::ffi::c_void, text: FlString) -> bool;
}

#[repr(C)]
#[derive(Debug)]
pub enum LayerType {
    Layer0 = 0,
    Layer1 = 1,
    Popup = 2,
    Count = 3,
}

#[repr(C)]
#[derive(Debug)]
pub struct Ui {
    pub dummy: u32,
}

impl Context {
    /// Set the active layer for rendering
    pub fn ui_set_layer(&self, layer: LayerType) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_ui_set_layer_impl(self_, layer);
        }
    }

    /// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
    pub fn ui_text(&self, text: &str) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_ui_text_impl(self_, FlString::new(text));
        }
    }

    /// Draw image. Images can be created with [Image::create_from_file] and [Image::create_from_memory]
    pub fn ui_image(&self, image: Image) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_ui_image_impl(self_, image);
        }
    }

    /// Draw image with given size
    pub fn ui_image_with_size(&self, image: Image, size: Vec2) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_ui_image_with_size_impl(self_, image, size);
        }
    }

    /// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)
    pub fn ui_set_pos(&self, pos: Vec2) {
        unsafe {
            let self_ = std::mem::transmute(self);
            fl_ui_set_pos_impl(self_, pos);
        }
    }

    /// Get the last widget size. This is usually used for doing manual layouting
    pub fn ui_get_last_widget_size(&self, pos: Vec2) -> Rect {
        unsafe {
            let self_ = std::mem::transmute(self);
            let ret_val = fl_ui_get_last_widget_size_impl(self_, pos);
            ret_val
        }
    }

    /// Push button widget that returns true if user has pressed it
    pub fn ui_push_button_with_icon(
        &self,
        text: &str,
        image: Image,
        text_pos: Vec2,
        image_scale: f32,
    ) -> bool {
        unsafe {
            let self_ = std::mem::transmute(self);
            let ret_val = fl_ui_push_button_with_icon_impl(
                self_,
                FlString::new(text),
                image,
                text_pos,
                image_scale,
            );
            ret_val
        }
    }

    /// Push button widget that returns true if user has pressed it
    pub fn ui_push_button(&self, text: &str) -> bool {
        unsafe {
            let self_ = std::mem::transmute(self);
            let ret_val = fl_ui_push_button_impl(self_, FlString::new(text));
            ret_val
        }
    }
}
