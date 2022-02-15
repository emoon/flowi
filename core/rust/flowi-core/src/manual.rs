// TODO: need to use cfg here for matching type
pub type IdxSize = u16;

#[repr(C)]
#[derive(Debug)]
pub struct FlString {
    pub str_ptr: *const u8,
    pub len: i32,
}

impl FlString {
    pub fn new(str_: &str) -> FlString {
        FlString {
            str_ptr: str_.as_ptr() as _,
            len: str_.len() as _,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Color {
    data: u32,
}

extern "C" {
    fn fl_error_last_error() -> FlString;
}

pub type Result<T> = core::result::Result<T, &'static str>;

pub fn get_last_error() -> &'static str {
    unsafe {
        "TODO: Correct error"
        //let text = fl_error_last_error();
        //let slice = core::slice::from_raw_parts(text.str_ptr, text.len as _);
        //core::str::from_utf8_unchecked(slice)
    }
}

/*
#[inline(always)]
pub fn get_handle(handle: u64) -> Result<u64> {
    if handle == 0 {
        Err(get_last_error())
    } else {
        Ok(handle)
    }
}

#[inline(always)]
pub(crate) fn get_const_ptr<'a, T>(ptr: *const T) -> Result<&'a T> {
    if ptr.is_null() {
        Err(get_last_error())
    } else {
        Ok(&*ptr)
    }
}

#[inline(always)]
pub(crate) fn get_mut_ptr<'a, T>(ptr: *mut T) -> Result<&'a mut T> {
    if ptr.is_null() {
        Err(get_last_error())
    } else {
        Ok(&mut *ptr)
    }
}
*/
