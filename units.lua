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

local GLFW_DIR = "flowi/cpp/external/glfw/"
local BIMG_DIR = "flowi/cpp/external/bimg/"
local BX_DIR = "flowi/cpp/external/bx/"
local DEAR_IMGUI_DIR = "core/c/external/dear-imgui"
local CORE_EXTERNAL_DIR = "core/c/external"

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

local function gen_moc(src)
    return Moc {
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

StaticLibrary {
    Name = "flowi",

	Includes = {
        BIMG_DIR .. "include",
		"flowi/cpp/include",
		"core/c/include",
		"core/c/include/flowi_core",
		"flowi/cpp/external/bx/include",
		"flowi/cpp/external/bgfx/include",
		"flowi/cpp/external/glfw/include",
		"flowi/cpp/external",
		DEAR_IMGUI_DIR,
        CORE_EXTERNAL_DIR,
        { BX_DIR .. "/include/compat/msvc" ; Config = "win64-*-*" },
	},

	Defines = {
	    -- TODO: Don't duplicate
        { "BX_CONFIG_DEBUG=1", "_DEBUG" ; Config = { "*-*-debug" } },
        { "BX_CONFIG_DEBUG=0" ; Config = { "*-*-release" } },
        "__STDC_LIMIT_MACROS",
        "__STDC_FORMAT_MACROS",
        "__STDC_CONSTANT_MACROS",
        "BGFX_CONFIG_RENDERER_WEBGPU=0",
        "BGFX_CONFIG_RENDERER_GNM=0",
        "BGFX_CONFIG_RENDERER_DIRECT3D11=0", -- Enable when we have a solution for dx shaders
        "BGFX_CONFIG_RENDERER_DIRECT3D12=0", -- Enable when we have a solution for dx shaders
        "BGFX_CONFIG_MULTITHREADED=0",
        { "BGFX_CONFIG_RENDERER_VULKAN=0", "BGFX_CONFIG_RENDERER_OPENGL=1" ; Config = { "linux-*-*", "win64-*-*" } },
        { "GLFW_EXPOSE_NATIVE_COCOA",
          "BGFX_CONFIG_RENDERER_VULKAN=0",
          "BGFX_CONFIG_RENDERER_METAL=1" ; Config = "macos-*-*" },
		{ "GLFW_EXPOSE_NATIVE_WIN32" ; Config = "win64-*-*" },
		{ "GLFW_EXPOSE_NATIVE_X11" ; Config = "linux-*-*" },
	},

    Sources = get_c_cpp_src("flowi/cpp/src"),
}

-----------------------------------------------------------------------------------------------------------------------

Program {
    Name = "flowi_testbed",

	Includes = {
		"flowi/cpp/include",
		"core/c/include/flowi_core",
		"core/c/include",
	},

    Sources = {
        "flowi/cpp/examples/testbed.c",
    },

    Env = {
		PROGCOM = {
            { "opengl32.lib", "shell32.lib", "gdi32.lib", "user32.lib"; Config = "win64-*-*" },
			{  "-lrt", "-ldl", "-lX11", "-lGL", "-lpthread", "-ldl"; Config = "linux-*-*" },
			{  "-lc++"; Config = "macosx-*-*" },
		},
	},

    Frameworks = { "Cocoa", "IOKit", "Metal", "QuartzCore", "MetalKit" },

    Depends = { "flowi", "flowi-core", "bgfx", "glfw", "freetype2" },
}

local FREETYPE2_LIB = "core/c/external/freetype2/"

-----------------------------------------------------------------------------------------------------------------------

Program {
    Name = "flowi_core_tests",

    Includes = {
        FREETYPE2_LIB .. "include",
        "core/c/include",
    },

    Sources = get_c_src("core/c/tests"),

    Env = {
        PROGCOM = {
            { "-lgcov"; Config = { "linux-gcc-*-test"} }
        },
    },

    Depends = { "flowi-core", "freetype2" },
}

-----------------------------------------------------------------------------------------------------------------------

Program {
    Name = "flowi_core_bench",

    Includes = {
        FREETYPE2_LIB .. "include",
        "core/c/include",
    },

    Sources = get_c_src("core/c/bench"),

    Depends = { "flowi-core", "freetype2" },
}

-----------------------------------------------------------------------------------------------------------------------
-- This is somewhat of a hack to generate the shader headers. There might be a better way to do this

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

Default "flowi_core_tests"
Default "flowi_core_bench"
Default "flowi_testbed"
Default "build_shaders"

-- vim: ts=4:sw=4:sts=4

