// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

extern "C" {
    fn fl_ui_text(text: FlString);
}

#[repr(C)]
pub struct Ui {
    data: Test,
}

impl Ui {
    pub fn text(text: &str) {
        unsafe {
            fl_ui_text(FlString::new(text));
        }
    }
}
