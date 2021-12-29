require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"

-----------------------------------------------------------------------------------------------------------------------

local FLOWI_DIR = "core/src/"
local FREETYPE2_LIB = "core/external/freetype2/"
local STB_LIB = "core/external/stb/"

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
    Name = "freetype2",

    Includes = {
        FREETYPE2_LIB .. "build/include",
        FREETYPE2_LIB .. "include",
        FREETYPE2_LIB .. "include/config",
    },

    Defines = {
        "FT2_BUILD_LIBRARY",
    },

    Sources = {
        FREETYPE2_LIB .. "src/autofit/autofit.c",
        FREETYPE2_LIB .. "src/base/ftsystem.c",
        FREETYPE2_LIB .. "src/base/ftbase.c",
        FREETYPE2_LIB .. "src/base/ftbbox.c",
        FREETYPE2_LIB .. "src/base/ftbdf.c",
        FREETYPE2_LIB .. "src/base/ftbitmap.c",
        FREETYPE2_LIB .. "src/base/ftcid.c",
        FREETYPE2_LIB .. "src/base/ftfstype.c",
        FREETYPE2_LIB .. "src/base/ftgasp.c",
        FREETYPE2_LIB .. "src/base/ftglyph.c",
        FREETYPE2_LIB .. "src/base/ftgxval.c",
        FREETYPE2_LIB .. "src/base/ftinit.c",
        FREETYPE2_LIB .. "src/base/ftmm.c",
        FREETYPE2_LIB .. "src/base/ftotval.c",
        FREETYPE2_LIB .. "src/base/ftpatent.c",
        FREETYPE2_LIB .. "src/base/ftpfr.c",
        FREETYPE2_LIB .. "src/base/ftstroke.c",
        FREETYPE2_LIB .. "src/base/ftsynth.c",
        FREETYPE2_LIB .. "src/base/fttype1.c",
        FREETYPE2_LIB .. "src/base/ftwinfnt.c",
        FREETYPE2_LIB .. "src/bdf/bdf.c",
        FREETYPE2_LIB .. "src/bzip2/ftbzip2.c",
        FREETYPE2_LIB .. "src/cache/ftcache.c",
        FREETYPE2_LIB .. "src/cff/cff.c",
        FREETYPE2_LIB .. "src/cid/type1cid.c",
        FREETYPE2_LIB .. "src/gzip/ftgzip.c",
        FREETYPE2_LIB .. "src/lzw/ftlzw.c",
        FREETYPE2_LIB .. "src/pcf/pcf.c",
        FREETYPE2_LIB .. "src/pfr/pfr.c",
        FREETYPE2_LIB .. "src/psaux/psaux.c",
        FREETYPE2_LIB .. "src/pshinter/pshinter.c",
        FREETYPE2_LIB .. "src/psnames/psnames.c",
        FREETYPE2_LIB .. "src/raster/raster.c",
        FREETYPE2_LIB .. "src/sdf/sdf.c",
        FREETYPE2_LIB .. "src/sfnt/sfnt.c",
        FREETYPE2_LIB .. "src/smooth/smooth.c",
        FREETYPE2_LIB .. "src/truetype/truetype.c",
        FREETYPE2_LIB .. "src/type1/type1.c",
        FREETYPE2_LIB .. "src/type42/type42.c",
        FREETYPE2_LIB .. "src/winfonts/winfnt.c",
        FREETYPE2_LIB .. "src/base/ftdebug.c",
        -- TODO: windows
        -- FREETYPE2_LIB .. "src/builds/windows/ftdebug.c",
    },
}

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
    Name = "flowi",

    Env = {
        CCOPTS = {
            { "-Wall", "-Wextra", "-Werror"; Config = "linux-*-*-*" },
            { "-fprofile-arcs", "-ftest-coverage"; Config = "linux-gcc-*-test" },
        },
    },

    Includes = {
        "core/external/freetype2/include",
    },

    Sources = {
        STB_LIB .. "stb.c",
        FLOWI_DIR .. "flowi.c",
        FLOWI_DIR .. "io.c",
        FLOWI_DIR .. "flowi_font.c",
        FLOWI_DIR .. "render.c",
        FLOWI_DIR .. "text.c",
        FLOWI_DIR .. "style.c",
        FLOWI_DIR .. "area.c",
        FLOWI_DIR .. "linear_allocator.c",
        FLOWI_DIR .. "primitives.c",
    },
}

