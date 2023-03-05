#![no_main]
#![no_std]

use flowi::Flowi;
/*
use core::panic::PanicInfo;

#[lang = "eh_personality"]
extern "C" fn eh_personality() {}

#[panic_handler]
fn panic(_panic: &PanicInfo<'_>) -> ! {
    loop {}
}
*/

#[no_mangle]
pub fn update(flowi: &Flowi) {
    let button = flowi.button();
    button.regular("Hello, world!");
}
