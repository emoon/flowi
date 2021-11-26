#pragma once

typedef enum FlError {
    FlError_None,
    FlError_Io,
    FlError_Memory,
    FlError_Font,
} FlError;

// Get current Fl error if any
FlError Floiw_error();

// Get the count of flowi errors
int Floiw_error_count();

// Get detailed error text report
const char* fl_get_detailed_error(int index, int* text_len);

