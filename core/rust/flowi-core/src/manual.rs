
// TODO: need to use cfg here for matching type
pub type IdxSize = u16;

#[repr(C)]
pub struct FlString {
    _str_ptr: *const u8,
    _len: i32,
}

impl FlString {
    pub fn new(str_: &str) -> FlString {
        FlString {
            _str_ptr: str_.as_ptr() as _,
            _len: str_.len() as _,
        }
    }
}

