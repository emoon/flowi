require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"

local native = require('tundra.native')

-----------------------------------------------------------------------------------------------------------------------

local GLFW_DIR = "testbed/external/glfw/"
local BIMG_DIR = "testbed/external/bimg/"
local BX_DIR = "testbed/external/bx/"

-- setup target for shader
local shaderc_platform = "windows"
local shaderc_vs_extra_params = " -p vs_5_0"
local shaderc_ps_extra_params = " -p ps_5_0"
if native.host_platform == "macosx" then
    shaderc_platform = "osx"
    shaderc_vs_extra_params = ""
    shaderc_ps_extra_params = ""
elseif native.host_platform == "linux" then
    shaderc_platform = "linux"
    shaderc_vs_extra_params = ""
    shaderc_ps_extra_params = ""
end

-----------------------------------------------------------------------------------------------------------------------

DefRule {
    Name = "ShadercFS",
    Command = "$(BGFX_SHADERC) -i external/bgfx/src -f $(<) -o $(@) --type fragment --platform " .. shaderc_platform .. shaderc_ps_extra_params,

    Blueprint = {
        Source = { Required = true, Type = "string", Help = "Input filename", },
        OutName = { Required = false, Type = "string", Help = "Output filename", },
    },

    Setup = function (env, data)
        return {
            InputFiles    = { data.Source },
            OutputFiles   = { "$(OBJECTDIR)/_generated/" .. tundra.path.drop_suffix(data.Source) .. ".fs" },
        }
    end,
}

-----------------------------------------------------------------------------------------------------------------------

DefRule {
    Name = "ShadercVS",
    Command = "$(BGFX_SHADERC) -i external/bgfx/src -f $(<) -o $(@) --type vertex --platform " .. shaderc_platform .. shaderc_vs_extra_params,

    Blueprint = {
        Source = { Required = true, Type = "string", Help = "Input filename", },
        OutName = { Required = false, Type = "string", Help = "Output filename", },
    },

    Setup = function (env, data)
        return {
            InputFiles    = { data.Source },
            OutputFiles   = { "$(OBJECTDIR)/_generated/" .. tundra.path.drop_suffix(data.Source) .. ".vs" },
        }
    end,
}

-----------------------------------------------------------------------------------------------------------------------

local function gen_moc(src)
    return Moc {
        Pass = "GenerateSources",
        Source = src
    }
end

-----------------------------------------------------------------------------------------------------------------------

local function gen_uic(src)
    return Uic {
        Pass = "GenerateSources",
        Source = src
    }
end

-----------------------------------------------------------------------------------------------------------------------

local function gen_rcc(src)
    return Rcc {
        Pass = "GenerateSources",
        Source = src
    }
end

-----------------------------------------------------------------------------------------------------------------------

local function get_rs_src(dir)
    return Glob {
        Dir = dir,
        Extensions = { ".rs" },
        Recursive = true,
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
    Name = "glfw",

    Env = {
        CPPPATH = {
            GLFW_DIR .. "src",
            GLFW_DIR .. "include",
        },

        CPPDEFS = {
            { "_GLFW_WIN32", "_GLFW_WGL", "WIN32"; Config = "win64-*-*" },
            { "_GLFW_X11", "_GLFW_GFX", "LINUX"; Config = "linux-*-*" },
            { "_GLFW_COCOA", "MACOSX"; Config = "macos-*-*" },
        },
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

Program {
    Name = "flowi_testbed",

	Includes = {
        BIMG_DIR .. "include",
		"testbed/external/bx/include",
		"testbed/external/bgfx/include",
		"testbed/external/glfw/include",
		"testbed/external",
        { BX_DIR .. "/include/compat/msvc" ; Config = "win64-*-*" },
	},

	Defines = {
		"BX_CONFIG_DEBUG=1",
        "__STDC_LIMIT_MACROS",
        "__STDC_FORMAT_MACROS",
        "__STDC_CONSTANT_MACROS",
        "_DEBUG",
		{ "GLFW_EXPOSE_NATIVE_WIN32" ; Config = "win64-*-*" },
		{ "GLFW_EXPOSE_NATIVE_COCOA" ; Config = "macos*-*-*" },
		{ "GLFW_EXPOSE_NATIVE_X11" ; Config = "linux-*-*" },
	},

    Sources = {
        "testbed/src/main.cpp",
        --Glob {
        --    Dir = "testbed/src",
        --    Extensions = { ".cpp" },
        --    Recursive = true,
        --},

        ShadercFS { Source = "testbed/shaders/color_fill.fs", OutName = "color_fill_fs.bin" },
        ShadercVS { Source = "testbed/shaders/color_fill.vs", OutName = "color_fill_vs.bin" },
        ShadercFS { Source = "testbed/shaders/fs_texture.sc", OutName = "fs_texture.bin" },
        ShadercVS { Source = "testbed/shaders/vs_texture.sc", OutName = "vs_texture.bin" },
    },

    Env = {
		PROGCOM = {
            { "opengl32.lib", "shell32.lib", "qtmain.lib", "gdi32.lib", "user32.lib"; Config = "win64-*-*" },
			{  "-lrt", "-ldl", "-lX11", "-lGL", "-lpthread", "-ldl"; Config = "linux-*-*" },
			{  "-lc++"; Config = "macosx-*-*" },
		},
	},

    Frameworks = { "Cocoa", "IOKit", "Metal", "QuartzCore", "MetalKit" },

    Depends = { "bgfx", "glfw", "flowi", "freetype2" },
}

local FLOWI_DIR = "core/src/"
local FREETYPE2_LIB = "core/external/freetype2/"

-----------------------------------------------------------------------------------------------------------------------

Program {
    Name = "flowi_core_tests",

    Includes = {
        -- FREETYPE2_LIB .. "build/include",
        FREETYPE2_LIB .. "include",
        -- FREETYPE2_LIB .. "include/config",
    },

    Sources = {
        "core/tests/tests.c",
    },

    Depends = { "flowi", "freetype2" },
}

Default "flowi_core_tests"
Default "flowi_testbed"

-- vim: ts=4:sw=4:sts=4

