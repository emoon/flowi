require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"

local native = require('tundra.native')

-----------------------------------------------------------------------------------------------------------------------

local function get_c_src(dir)
    return Glob {
        Dir = dir,
        Extensions = { ".c", ".h" },
        Recursive = true,
}
end

-----------------------------------------------------------------------------------------------------------------------

local GLFW_DIR = "external/glfw/"
local BIMG_DIR = "external/bimg/"
local BX_DIR = "external/bx/"
local DEAR_IMGUI_DIR = "external/dear-imgui"
local EXTERNAL_DIR = "external"
local FREETYPE2_LIB = "external/freetype2/"
local NANOSVG_LIB = "external/nanosvg/"
local STB_LIB = "external/stb/"
local TLSF_LIB = "external/tlsf/"
local EXTERNAL_PATH = "external"
local DEAR_IMGUI = "external/dear-imgui/"

-----------------------------------------------------------------------------------------------------------------------

DefRule {
    Name = "ShaderC",
    Command = "$(BGFX_SHADERC) $(COMPILE_PARAMS) -i core/c/external/bgfx/src -f $(<) --bin2c -o $(@)",
    Pass = "BuildTools",
    ConfigInvariant = true,

    Blueprint = {
        Parameters = { Required = true, Type = "string", Help = "Compile parameters", },
        Source = { Required = true, Type = "string", Help = "Input filename", },
        OutName = { Required = false, Type = "string", Help = "Output filename", },
    },

    Setup = function (env, data)
        env:set('COMPILE_PARAMS', data.Parameters)
        return {
            InputFiles    = { data.Source },
            OutputFiles   = { data.OutName },
        }
    end,
}

-----------------------------------------------------------------------------------------------------------------------

local function build_vs(src, dest)
    return {
        ShaderC { Source = src, OutName = dest .. "_glsl.h", Parameters = "--type vertex --platform linux -p 120" },
        ShaderC { Source = src, OutName = dest .. "_essl.h", Parameters = "--type vertex --platform android" },
        ShaderC { Source = src, OutName = dest .. "_spv.h", Parameters = "--type vertex --platform linux -p spirv" },
        ShaderC { Source = src, OutName = dest .. "_mtl.h", Parameters = "--type vertex --platform osx -p metal -O 3" },
        {
            {
                ShaderC { Source = src, OutName = dest .. "_dx9.h", Parameters = "--type vertex --platform windows -p vs_3_0 -O 3" },
                ShaderC { Source = src, OutName = dest .. "_dx11.h", Parameters = "--type vertex --platform windows -p vs_4_0 -O 3" }
                ; Config = "win64-*-*",
            },
        },
    }
end

-----------------------------------------------------------------------------------------------------------------------

local function build_fs(src, dest)
    return {
        ShaderC { Source = src, OutName = dest .. "_glsl.h", Parameters = "--type fragment --platform linux -p 120" },
        ShaderC { Source = src, OutName = dest .. "_essl.h", Parameters = "--type fragment --platform android" },
        ShaderC { Source = src, OutName = dest .. "_spv.h", Parameters = "--type fragment --platform linux -p spirv" },
        ShaderC { Source = src, OutName = dest .. "_mtl.h", Parameters = "--type fragment --platform osx -p metal -O 3" },
        {
            {
                ShaderC { Source = src, OutName = dest .. "_dx9.h", Parameters = "--type fragment --platform windows -p ps_3_0 -O 3" },
                ShaderC { Source = src, OutName = dest .. "_dx11.h", Parameters = "--type fragment --platform windows -p ps_4_0 -O 3" }
                ; Config = "win64-*-*",
            },
        },
    }
end

-----------------------------------------------------------------------------------------------------------------------

local function get_c_cpp_src(dir)
    return Glob {
        Dir = dir,
        Extensions = { ".cpp", ".c", ".h" },
        Recursive = true,
}
end

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

local glfw_defines = {
    { "GLFW_EXPOSE_NATIVE_WIN32", "_GLFW_WIN32", "_GLFW_WGL", "WIN32"; Config = "win64-*-*" },
    { "GLFW_EXPOSE_NATIVE_X11", "_GLFW_X11", "_GLFW_GFX", "LINUX"; Config = "linux-*-*" },
    { "GLFW_EXPOSE_NATIVE_COCOA", "_GLFW_COCOA", "MACOSX"; Config = "macos-*-*" },
}

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
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

        {
            GLFW_DIR .. "src/cocoa_init.m",
            GLFW_DIR .. "src/cocoa_joystick.m",
            GLFW_DIR .. "src/cocoa_monitor.m",
            GLFW_DIR .. "src/cocoa_time.c",
            GLFW_DIR .. "src/cocoa_window.m",
            GLFW_DIR .. "src/posix_thread.c",
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

-----------------------------------------------------------------------------------------------------------------------

local flowi_includes = {
    BIMG_DIR .. "include",
    "external/bgfx/include",
    "external/bx/include",
    "flowi/cpp/external",
    "flowi/cpp/external/bgfx/include",
    "flowi/cpp/external/bx/include",
    "flowi/cpp/external/glfw/include",
    "include",
    GLFW_DIR .. "include",
    STB_LIB,
    NANOSVG_LIB,
    DEAR_IMGUI_DIR,
    EXTERNAL_DIR,
    { BX_DIR .. "/include/compat/msvc" ; Config = "win64-*-*" },
}

-----------------------------------------------------------------------------------------------------------------------

local flowi_sources = {
    get_c_cpp_src("src"),
    STB_LIB .. "stb.c",
    NANOSVG_LIB .. "nanosvg.c",
    get_c_cpp_src(DEAR_IMGUI),
}

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
    Name = "flowi",

	Includes = flowi_includes,

	Defines = { "FLOWI_STATIC" },

    Depends = { "bgfx" },

    Sources = flowi_sources,
}

-----------------------------------------------------------------------------------------------------------------------

SharedLibrary {
    Name = "flowi-shared",

	Includes = flowi_includes,

	Defines = { "FLOWI_SHARED" },

    Depends = { "bgfx", "glfw", "freetype2" },

    Env = {
		SHLIBCOM = {
            { "opengl32.lib", "shell32.lib", "gdi32.lib", "user32.lib"; Config = "win64-*-*" },
			{  "-lrt", "-ldl", "-lX11", "-lGL", "-lpthread", "-ldl"; Config = "linux-*-*" },
			{  "-lc++"; Config = "macosx-*-*" },
		},
	},

    Sources = flowi_sources,
}

-----------------------------------------------------------------------------------------------------------------------

Program {
    Name = "flowi_testbed",

	Includes = {
		"include",
	},

    Sources = {
        "examples/testbed.c",
    },

    Env = {
		PROGCOM = {
            { "opengl32.lib", "shell32.lib", "gdi32.lib", "user32.lib"; Config = "win64-*-*" },
			{  "-lrt", "-ldl", "-lX11", "-lGL", "-lpthread", "-ldl"; Config = "linux-*-*" },
			{  "-lc++"; Config = "macosx-*-*" },
		},
	},

    Frameworks = { "Cocoa", "IOKit", "Metal", "QuartzCore", "MetalKit" },

    Depends = { "flowi", "bgfx", "glfw", "freetype2" },
}

-----------------------------------------------------------------------------------------------------------------------
-- This is somewhat of a hack to generate the shader headers. There might be a better way to do this
--[[

Program {
    Name = "build_shaders",
    Pass = "GenerateSources",

    Sources = {
        "flowi/cpp/shaders/dummy.c",

        build_vs("flowi/cpp/shaders/color_fill.vs", "flowi/cpp/shaders/generated/color_fill_vs"),
        build_fs("flowi/cpp/shaders/color_fill.fs", "flowi/cpp/shaders/generated/color_fill_fs"),

        build_vs("flowi/cpp/shaders/vs_texture.sc", "flowi/cpp/shaders/generated/vs_texture"),
        build_fs("flowi/cpp/shaders/fs_texture.sc", "flowi/cpp/shaders/generated/fs_texture"),

        build_vs("flowi/cpp/shaders/vs_texture_r.sc", "flowi/cpp/shaders/generated/vs_texture_r"),
        build_fs("flowi/cpp/shaders/fs_texture_r.sc", "flowi/cpp/shaders/generated/fs_texture_r"),
    }
}
--]]

-----------------------------------------------------------------------------------------------------------------------

Default "flowi-shared"
Default "flowi_testbed"
-- Default "build_shaders"

-- vim: ts=4:sw=4:sts=4

