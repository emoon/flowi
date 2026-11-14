# Embeds a .glsl file as a NUL-terminated C byte array. Run at build time:
#   cmake -DINPUT=<file.glsl> -DOUTPUT=<file.c> -DSYMBOL=<name> -P embed_glsl.cmake
#
# gpu_shaders.c composes the compute shaders from these arrays at runtime.

file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
get_filename_component(in_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
    "// Generated from ${in_name} — do not edit; edit the .glsl source instead.\n"
    "const char ${SYMBOL}[] = { ${bytes}0x00 };\n")
