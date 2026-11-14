# Standalone SDL + GL window driving the internal GPU backend. A development tool that needs GPU hardware.

# Embeds a binary asset as a C byte-array header at configure time, so the demo loads its font and
# SVG icons from memory without a VFS mount. The headers are written to the build tree.
# Roboto is Apache-2.0; the SVGs are first-party.
function(embed_asset src_path out_header var_name)
    file(READ ${src_path} _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes ${_hex})
    get_filename_component(_name ${src_path} NAME)
    file(WRITE ${out_header}
        "// Generated from assets/${_name} at CMake configure time — do not edit.\n"
        "static const unsigned char ${var_name}[] = {${_bytes}};\n")
endfunction()

embed_asset(${CMAKE_SOURCE_DIR}/examples/data/Roboto-Regular.ttf
    ${CMAKE_CURRENT_BINARY_DIR}/roboto_regular_ttf.h k_roboto_regular_ttf)
embed_asset(${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_mono.svg
    ${CMAKE_CURRENT_BINARY_DIR}/icon_mono_svg.h k_icon_mono_svg)
embed_asset(${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_color.svg
    ${CMAKE_CURRENT_BINARY_DIR}/icon_color_svg.h k_icon_color_svg)

add_executable(gpu_window_example main.c)
target_include_directories(gpu_window_example PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(gpu_window_example PRIVATE flowi_objects)
apply_target_config(gpu_window_example)
