#![no_std]

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let result = 2 + 2;
        assert_eq!(result, 4);
    }
}

extern "C" {
    pub fn fli_render_font(dest: *mut u32, width: u32, height: u32);
}

