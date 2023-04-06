local native = require('tundra.native')
require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"

-----------------------------------------------------------------------------------------------------------------------

local glfw_defines = {
    { "GLFW_EXPOSE_NATIVE_WIN32", "_GLFW_WIN32", "_GLFW_WGL", "WIN32"; Config = "win64-*-*" },
    { "GLFW_EXPOSE_NATIVE_X11", "_GLFW_X11", "_GLFW_GFX", "LINUX"; Config = "linux-*-*" },
    { "GLFW_EXPOSE_NATIVE_COCOA", "_GLFW_COCOA", "MACOSX"; Config = "macos-*-*" },
}

-----------------------------------------------------------------------------------------------------------------------

local mac_opts = {
    "-Wall", "-I.",
    "-DFLOWI_MAC",
    { "-DFLOWI_DEBUG", "-O0", "-g"; Config = "*-*-debug" },
    { "-DFLOWI_DEBUG", "-O0", "-fsanitize=address", "-fno-omit-frame-pointer", "-g"; Config = "*-*-debug-asan" },
    { "-DFLOWI_RELEASE", "-O3", "-g"; Config = "*-*-release" },
}

-----------------------------------------------------------------------------------------------------------------------

local macosx = {
    Env = {
        CCOPTS =  {
            mac_opts,
        },

        CXXOPTS = {
            mac_opts,
            "-std=c++14",
        },

        SHLIBOPTS = {
			"-lstdc++",
			{ "-fsanitize=address"; Config = "*-*-debug-asan" },
		},

        PROGCOM = {
			"-lstdc++",
			{ "-fsanitize=address"; Config = "*-*-debug-asan" },
		},
    },

    Frameworks = {
        { "Cocoa" },
        { "Metal" },
        { "QuartzCore" },
    },
}

-----------------------------------------------------------------------------------------------------------------------
--    Re -gdwarf-4 instead of -g:
--    The oldest versions of supported compilers default to DWARF-4, but
--    newer versions may default to DWARF-5 or newer (e.g. clang 14), which
--    Valgrind doesn't support.

local gcc_opts = {
    "-I.",
    "-Wno-array-bounds",
    "-Wno-attributes",
    "-Wno-unused-value",
    "-DOBJECT_DIR=\\\"$(OBJECTDIR)\\\"",
    "-I$(OBJECTDIR)",
    "-Wall",
    "-fPIC",
    "-msse2",   -- TODO: Separate gcc options for x64/arm somehow?
    { "-DFLOWI_TEST", "-O2", "-gdwarf-4"; Config = "*-*-test" },
    { "-DFLOWI_DEBUG", "-O0", "-gdwarf-4"; Config = "*-*-debug" },
    { "-DFLOWI_RELEASE", "-O3", Config = "*-*-release" },
}

local gcc_env = {
    Env = {
		FLOWI_VERSION = native.getenv("FLOWI_VERSION", "Development Version"),
        CCOPTS = {
			"-Werror=incompatible-pointer-types",
            gcc_opts,
        },

        CXXOPTS = {
            gcc_opts,
            "-DFLOWI_VERSION='\"$(FLOWI_VERSION)\"'",
        },
    },

    ReplaceEnv = {
        LD = "c++",
    },
}

-----------------------------------------------------------------------------------------------------------------------

local win64_opts = {
    "/EHsc", "/FS", "/MD", "/W3", "/I.", "/DUNICODE", "/D_UNICODE", "/DWIN32", "/D_CRT_SECURE_NO_WARNINGS",
    "\"/DOBJECT_DIR=$(OBJECTDIR:#)\"",
    { "/DFLOWI_DEBUG","/Od"; Config = "*-*-debug" },
    { "/DFLOWI_RELEASE", "/O2"; Config = "*-*-release" },
}

local win64 = {
    Env = {
		FLOWI_VERSION = native.getenv("FLOWI_VERSION", "Development Version"),

        GENERATE_PDB = "1",
        CCOPTS = {
            win64_opts,
        },

        CXXOPTS = {
            win64_opts,
        },

        OBJCCOM = "meh",
    },
}

-----------------------------------------------------------------------------------------------------------------------

local FLOWI_DIR = "c_cpp/"
local GLFW_DIR = "external/glfw/"
local FREETYPE2_LIB = "external/freetype2/"
local STB_DIR = "external/stb/"
local NANOSVG_DIR = "external/nanosvg/"
local EXTERNAL_PATH = "external"
local DEAR_IMGUI = "external/dear-imgui/"
local DEAR_IMGUI_DIR = "external/dear-imgui/"
local BIMG_DIR = "external/bimg/"
local BX_DIR = "external/bx/"
local BGFX_DIR = "external/bgfx/"
local EXTERNAL_DIR = "external"

-----------------------------------------------------------------------------------------------------------------------

Build {
    Units = function ()
        local freetype_lib = StaticLibrary {
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
                FREETYPE2_LIB .. "src/svg/ftsvg.c",
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

        --[[
        local bgfx_lib = StaticLibrary {
            Name = "bgfx",

            Includes = {
                {
                    BX_DIR .. "include/compat/msvc",
                    BGFX_DIR .. "3rdparty/dxsdk/include" ; Config = "win64-*-*"
                },

                {
                    BX_DIR .. "include/compat/osx" ; Config = "macos-*-*"
                },

                BGFX_DIR .. "3rdparty/khronos",
                BGFX_DIR .. "3rdparty",
                BGFX_DIR .. "include",
                BX_DIR .. "include",
                BX_DIR .. "3rdparty",
                BIMG_DIR .. "include",
                BIMG_DIR .. "3rdparty",
                BIMG_DIR .. "3rdparty/iqa/include",
                BIMG_DIR .. "3rdparty/astc-encoder/include",
                BIMG_DIR .. "3rdparty/tinyexr/deps/miniz",
                EXTERNAL_DIR,
            },

            Env = {
                CXXOPTS = {
                    { "-Wno-variadic-macros", "-Wno-everything" ; Config = "macos-*-*" },
                    { "-fno-exceptions" ; Config = { "macos-*-*", "linux-*-*" } },
                    { "/EHsc"; Config = "win64-*-*" },
                },
            },

            Defines = bgfx_defines,

            Propagate = {
                Defines = bgfx_defines,
            },

            Sources = {
                BIMG_DIR .. "src/image.cpp",
                BIMG_DIR .. "src/image_cubemap_filter.cpp",
                BIMG_DIR .. "src/image_decode.cpp",
                BIMG_DIR .. "src/image_gnf.cpp",
                BX_DIR .. "src/amalgamated.cpp",
                BGFX_DIR .. "src/bgfx.cpp",
                BGFX_DIR .. "src/vertexlayout.cpp",
                BGFX_DIR .. "src/debug_renderdoc.cpp",
                BGFX_DIR .. "src/topology.cpp",
                BGFX_DIR .. "src/shader_dx9bc.cpp",
                BGFX_DIR .. "src/shader_dxbc.cpp",
                BGFX_DIR .. "src/shader.cpp",
                BGFX_DIR .. "src/shader_spirv.cpp",
                BGFX_DIR .. "src/renderer_agc.cpp",
                BGFX_DIR .. "src/renderer_gnm.cpp",
                BGFX_DIR .. "src/renderer_webgpu.cpp",
                BGFX_DIR .. "src/renderer_nvn.cpp",
                BGFX_DIR .. "src/renderer_gl.cpp",
                BGFX_DIR .. "src/renderer_vk.cpp",
                BGFX_DIR .. "src/renderer_noop.cpp",
                BGFX_DIR .. "src/renderer_d3d9.cpp",
                BGFX_DIR .. "src/renderer_d3d11.cpp",
                BGFX_DIR .. "src/renderer_d3d12.cpp",
                {
                    BGFX_DIR .. "src/glcontext_nsgl.mm",
                    BGFX_DIR .. "src/renderer_mtl.mm" ; Config = "macos-*-*"
                },
                {
                    BGFX_DIR .. "src/glcontext_wgl.cpp",
                    BGFX_DIR .. "src/nvapi.cpp",
                    BGFX_DIR .. "src/dxgi.cpp" ; Config = "win64-*-*"
                },
                {
                    BGFX_DIR .. "src/glcontext_glx.cpp" ; Config = "linux-*-*"
                },
            },
        }
        --]]

        local glfw_lib = StaticLibrary {
            Name = "glfw",

            Includes = {
                GLFW_DIR .. "src",
                GLFW_DIR .. "include",
            },

            Defines = glfw_defines,

            Propagate = {
                Defines = glfw_defines,
            },

            Sources = {
                GLFW_DIR .. "src/window.c",
                GLFW_DIR .. "src/context.c",
                GLFW_DIR .. "src/init.c",
                GLFW_DIR .. "src/input.c",
                GLFW_DIR .. "src/monitor.c",
                GLFW_DIR .. "src/vulkan.c",
                GLFW_DIR .. "src/osmesa_context.c",
                GLFW_DIR .. "src/egl_context.c",
                GLFW_DIR .. "src/platform.c",
                GLFW_DIR .. "src/null_init.c",
                GLFW_DIR .. "src/null_window.c",
                GLFW_DIR .. "src/null_monitor.c",
                GLFW_DIR .. "src/null_joystick.c",

                {
                    GLFW_DIR .. "src/cocoa_init.m",
                    GLFW_DIR .. "src/cocoa_joystick.m",
                    GLFW_DIR .. "src/cocoa_monitor.m",
                    GLFW_DIR .. "src/cocoa_time.c",
                    GLFW_DIR .. "src/cocoa_window.m",
                    GLFW_DIR .. "src/posix_thread.c",
                    GLFW_DIR .. "src/posix_module.c",
                    GLFW_DIR .. "src/nsgl_context.h",
                    GLFW_DIR .. "src/nsgl_context.m" ; Config = "macos-*-*"
                },

                {
                    GLFW_DIR .. "src/glx_context.c",
                    -- GLFW_DIR .. "src/wl_init.c",
                    --GLFW_DIR .. "src/wl_monitor.c",
                    --GLFW_DIR .. "src/wl_window.c",
                    GLFW_DIR .. "src/x11_init.c",
                    GLFW_DIR .. "src/x11_monitor.c",
                    GLFW_DIR .. "src/x11_window.c",
                    GLFW_DIR .. "src/linux_joystick.c",
                    GLFW_DIR .. "src/posix_thread.c",
                    GLFW_DIR .. "src/posix_time.c",
                    GLFW_DIR .. "src/posix_module.c",
                    GLFW_DIR .. "src/posix_poll.c",
                    GLFW_DIR .. "src/xkb_unicode.c" ; Config = "linux-*-*",
                },

                {
                    GLFW_DIR .. "src/wgl_context.c",
                    GLFW_DIR .. "src/win32_init.c",
                    GLFW_DIR .. "src/win32_joystick.c",
                    GLFW_DIR .. "src/win32_monitor.c",
                    GLFW_DIR .. "src/win32_thread.c",
                    GLFW_DIR .. "src/win32_time.c",
                    GLFW_DIR .. "src/win32_window.c" ; Config = "win64-*-*",
                },
            },
        }

        local ui_lib = StaticLibrary {
            Name = "ui",

            Defines = {
                glfw_defines,
            },

            Includes = {
                "langs/c_cpp/include",
                "external/glfw/include",
                "external/bgfx/include",
                "external/bx/include",
                "external",
                "external/nanosvg",
                "external/dear-imgui",
                "external/stb",
                "external/freetype2/include",
            },

            Sources = {
                DEAR_IMGUI_DIR .. "imgui.cpp",
                DEAR_IMGUI_DIR .. "imgui_draw.cpp",
                DEAR_IMGUI_DIR .. "imgui_tables.cpp",
                DEAR_IMGUI_DIR .. "imgui_widgets.cpp",
                DEAR_IMGUI_DIR .. "misc/freetype/imgui_freetype.cpp",
                --FLOWI_DIR .. "io.cpp",
                --FLOWI_DIR .. "application.cpp",
                --FLOWI_DIR .. "flowi.cpp",
                FLOWI_DIR .. "font.cpp",
                FLOWI_DIR .. "imgui_impl_glfw.cpp",
                FLOWI_DIR .. "imgui_wrap.cpp",
                FLOWI_DIR .. "style.cpp",
                --FLOWI_DIR .. "image.c",
                FLOWI_DIR .. "area.c",
                FLOWI_DIR .. "array.c",
                --FLOWI_DIR .. "atlas.c",
                FLOWI_DIR .. "command_buffer.c",
                FLOWI_DIR .. "handles.c",
                --FLOWI_DIR .. "io.c",
                FLOWI_DIR .. "layer.c",
                FLOWI_DIR .. "linear_allocator.c",
                FLOWI_DIR .. "primitive_rect.c",
                FLOWI_DIR .. "string_allocator.c",
                FLOWI_DIR .. "text.c",
                FLOWI_DIR .. "vertex_allocator.c",
                NANOSVG_DIR .. "nanosvg.c",
                STB_DIR .. "stb.c",
                { FLOWI_DIR .. "metal_workaround.mm" ; Config = "macos-*-*" },
            },

            Depends = {
                glfw_lib,
                -- bgfx_lib,
            },
        }

        Default(glfw_lib)
        -- Default(bgfx_lib)
        Default(freetype_lib)
        Default(ui_lib)
    end,

    Configs = {
        Config { Name = "macos-clang", DefaultOnHost = "macosx", Inherit = macosx, Tools = { "clang-osx" } },
        Config { Name = "win64-msvc", DefaultOnHost = { "windows" }, Inherit = win64, Tools = { "msvc-vs2019" } },
        Config { Name = "linux-clang", DefaultOnHost = { "linux" }, Inherit = gcc_env, Tools = { "clang" } },
    },

    Variants = { "debug", "release" },
    SubVariants = { "default", "test" },
}

-- vim: ts=4:sw=4:sts=4


