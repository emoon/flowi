# Embed a .glsl file as a NUL-terminated C byte array, run at build time via:
#   cmake -DINPUT=<file.glsl> -DOUTPUT=<file.glsl.c> -DSYMBOL=<name> -P embed_glsl.cmake
#
# The GPU backend composes its compute shaders from these arrays at runtime (gpu_shaders.c). Embedding the
# pure-GLSL sources as bytes keeps the shaders out of C string literals (so clang-format never mangles them)
# while staying self-contained — no runtime file IO.

file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
get_filename_component(in_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
    "// Generated from ${in_name} — do not edit; edit the .glsl source instead.\n"
    "const char ${SYMBOL}[] = { ${bytes}0x00 };\n")
