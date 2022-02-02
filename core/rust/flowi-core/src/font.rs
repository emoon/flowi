
// This file is auto-generated by api_gen. DO NOT EDIT!

#[allow(unused_imports)]
use crate::*;

extern "C" {
    fn fl_font_new_from_file_impl(filename: FlString, font_size: u32, placement_mode: FontPlacementMode) -> Font;
    fn fl_font_new_from_memory_impl(name: FlString, data: u8, font_size: u32, placement_mode: FontPlacementMode) -> Font;
    fn fl_font_set_impl(self_c: *mut Font);
    fn fl_font_set_with_size_impl(self_c: *mut Font, size: u32);
    fn fl_font_destroy_impl(self_c: *mut Font);
}

/// Allows the user to select how accurate the glyph placement should be.The list has a the fastest (CPU performance wise) first (Monospace) and the slowest (Accurate) lastRule of thumb is:Auto (Same as basic)Monospaced (code/fixed size fonts) - use Monospace modeRegular Latin text - use Basic modeHebrew and other complex languages that require accurate layout - Use accurate
#[repr(C)]
pub enum FontPlacementMode {
    /// Let the library decide the mode (default)
    Auto = 0,
    /// Used for regular Latin based text
    Basic = 0,
    /// Used for fixed-width monospaces fonts (Fastest)
    Mono = 1,
    /// Used for accurate glyph placement (uses the Harfbuzz lib thus is the slowest mode)
    Accurate = 2,
}

#[repr(C)]
pub struct Font {
}

impl Font {
/// Create a font from (TTF) file. To use the font use `fl_font_set(id)` before using text-based widgetsGlyphRanges can be set to NULL if AtlasMode is BuildOnDemandReturns >= 0 for valid handle, use fl_get_status(); for more detailed error message
pub fn new_from_file(filename: &str, font_size: u32, placement_mode: FontPlacementMode) {
unsafe { 
fl_font_new_from_file_impl(FlString::new(filename), font_size, placement_mode);
}
}

/// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some casesLike when needing the accurate placement mode used by Harzbuff that needs to original ttf data