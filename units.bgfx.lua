require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"
require "tundra.syntax.rust-cargo"

local native = require('tundra.native')

local BIMG_DIR = "external/bimg/"
local BX_DIR = "external/bx/"
local BGFX_DIR = "external/bgfx/"
local EXTERNAL_DIR = "external"

local GLSL_OPTIMIZER = BGFX_DIR  .. "3rdparty/glsl-optimizer/"
local FCPP_DIR = BGFX_DIR .. "3rdparty/fcpp/"
local SPIRV_CROSS = BGFX_DIR .. "3rdparty/spirv-cross/"
local GLSLANG_DIR = BGFX_DIR .. "3rdparty/glslang/"
local SPIRV_TOOLS = BGFX_DIR .. "3rdparty/spirv-tools/"
local SPIRV_HEADERS = BGFX_DIR .. "3rdparty/spirv-headers/"

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

local function get_c_cpp_src(dir)
    return Glob {
        Dir = dir,
        Extensions = { ".cpp", ".c", ".h" },
        Recursive = true,
    }
end

local function glob_no_main(dir, ext, rec)
    return FGlob {
        Dir = dir,
        Extensions = { ext, ".h" },
        Filters = {
            { Pattern = "main.cpp", Config = "never" }
        },
        Recursive = rec,
    }
end

-----------------------------------------------------------------------------------------------------------------------

local function glob_c_cpp_no_main(dir)
    return FGlob {
        Dir = dir,
        Extensions = { ".cpp", ".c", ".h" },
        Filters = {
            { Pattern = "main.cpp", Config = "never" }
        },
        Recursive = false,
    }
end

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
    Name = "glslang",
    Pass = "BuildTools",

    Env = {
        CXXOPTS = {
            { "-fno-strict-aliasing"; Config = { "macos-*-*", "linux-*-*" } },
        },
    },

    Defines = {
        "ENABLE_OPT=1",
        "ENABLE_HLSL=1",
    },

    Includes = {
        GLSLANG_DIR .. "glslang/Include",
        GLSLANG_DIR .. "glslang",
        GLSLANG_DIR,
        BGFX_DIR .. "3rdparty",
        SPIRV_TOOLS .. "include",
    },

    Sources = {
        get_c_cpp_src(GLSLANG_DIR .. "OGLCompilersDLL"),
        get_c_cpp_src(GLSLANG_DIR .. "StandAlone"),
        get_c_cpp_src(GLSLANG_DIR .. "glslang/GenericCodeGen"),
        get_c_cpp_src(GLSLANG_DIR .. "glslang/MachineIndependent"),
        get_c_cpp_src(GLSLANG_DIR .. "SPIRV"),
        get_c_cpp_src(GLSLANG_DIR .. "glslang/HLSL"),
        get_c_cpp_src(GLSLANG_DIR .. "glslang/CInterface"),
        { get_c_cpp_src(GLSLANG_DIR .. "glslang/OSDependent/Windows") ; Config = "win64-*-*" },
        { get_c_cpp_src(GLSLANG_DIR .. "glslang/OSDependent/Unix") ; Config = { "linux-*-*", "macos-*-*" } },
    }
}

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
    Name = "spirv_tools",
    Pass = "BuildTools",

    Env = {
        CXXOPTS = {
            { "-fno-strict-aliasing"; Config = { "macos-*-*", "linux-*-*" } },
        },
    },

    Includes = {
        SPIRV_TOOLS,
        SPIRV_TOOLS .. "include",
        SPIRV_TOOLS .. "include/generated",
        SPIRV_HEADERS .. "include",
    },

    Sources = {
        glob_no_main(SPIRV_TOOLS .. "source", ".cpp", false),
        glob_no_main(SPIRV_TOOLS .. "source/opt", ".cpp", false),
        glob_no_main(SPIRV_TOOLS .. "source/util", ".cpp", false),
        glob_no_main(SPIRV_TOOLS .. "source/val", ".cpp", false),
    }
}

-----------------------------------------------------------------------------------------------------------------------

Program {
    Name = "bgfx_shaderc",
    Target = "$(BGFX_SHADERC)",
    Pass = "BuildTools",

    Env = {
        CCOPTS = {
            { "/wd4291", "/W3", "-D__STDC__", "-D__STDC_VERSION__=199901L", "-Dstrdup=_strdup", "-Dalloca=_alloca", "-Disascii=__isascii"; Config = "win64-*-*" },
            { "-Wno-everything"; Config = "macos-*-*" },
            { "-fno-strict-aliasing"; Config = { "macos-*-*", "linux-*-*" } },
        },

        CXXOPTS = {
            { "/wd4291", "/W3", "-D__STDC__", "-D__STDC_VERSION__=199901L", "-Dstrdup=_strdup", "-Dalloca=_alloca", "-Disascii=__isascii"; Config = "win64-*-*" },
            { "-Wno-everything"; Config = "macos-*-*" },
            { "-fno-strict-aliasing"; Config = { "macos-*-*", "linux-*-*" } },
        },

        CPPDEFS = {
            { "BX_CONFIG_DEBUG=0", "NINCLUDE=64", "NWORK=65536", "NBUFF=65536", "OLD_PREPROCESSOR=0" },
        },

        CPPPATH = {
            {
                BIMG_DIR .. "include",
                SPIRV_CROSS,
                BX_DIR .. "3rdparty",
                BX_DIR .. "include",
                BGFX_DIR  .. "3rdparty/glslang/glslang/Public",
                BGFX_DIR  .. "3rdparty/glslang/glslang/Include",
                BGFX_DIR  .. "3rdparty/glslang",
                BGFX_DIR  .. "3rdparty/spirv-cross",
                BGFX_DIR  .. "3rdparty/spirv-tools/include",
                BGFX_DIR .. "include",
                BGFX_DIR  .. "3rdparty/webgpu/include",
                FCPP_DIR,
                GLSL_OPTIMIZER .. "src",
                GLSL_OPTIMIZER .. "include",
                GLSL_OPTIMIZER .. "src/mesa",
                GLSL_OPTIMIZER .. "src/mapi",
                GLSL_OPTIMIZER .. "src/glsl",
                GLSL_OPTIMIZER .. "src/glsl/glcpp",
            },

            {
                BX_DIR .. "include/compat/osx" ; Config = "macos-*-*"
            },

            {
                BX_DIR .. "include/compat/msvc"; Config = "win64-*-*"
            },
        },--"

        PROGCOM = {
            { "-lstdc++"; Config = { "macos-clang-*", "linux-gcc-*" } },
            { "-lm -lpthread -ldl -lX11"; Config = "linux-*-*" },
        },
    },

    Sources = {
        BX_DIR .. "src/amalgamated.cpp",
        BGFX_DIR .. "tools/shaderc/shaderc.cpp",
        BGFX_DIR .. "tools/shaderc/shaderc_spirv.cpp",
        BGFX_DIR .. "tools/shaderc/shaderc_glsl.cpp",
        BGFX_DIR .. "tools/shaderc/shaderc_hlsl.cpp",
        BGFX_DIR .. "tools/shaderc/shaderc_pssl.cpp",
        BGFX_DIR .. "tools/shaderc/shaderc_metal.cpp",
        BGFX_DIR .. "src/vertexlayout.cpp",
        BGFX_DIR .. "src/vertexlayout.h",
        BGFX_DIR .. "src/shader_spirv.cpp",
        BGFX_DIR .. "src/shader.cpp",
        BGFX_DIR .. "src/shader_dxbc.cpp",
        BGFX_DIR .. "src/shader_dx9bc.cpp",

        FCPP_DIR .. "cpp1.c",
        FCPP_DIR .. "cpp2.c",
        FCPP_DIR .. "cpp3.c",
        FCPP_DIR .. "cpp4.c",
        FCPP_DIR .. "cpp5.c",
        FCPP_DIR .. "cpp6.c",

        glob_no_main(SPIRV_CROSS, ".cpp", true),
        glob_no_main(GLSL_OPTIMIZER .. "src/mesa", ".c", true),
        glob_no_main(GLSL_OPTIMIZER .. "src/util",  ".c", true),
        glob_c_cpp_no_main(GLSL_OPTIMIZER .. "src/glsl"),
        glob_c_cpp_no_main(GLSL_OPTIMIZER .. "src/glsl/glcpp"),
    },

    Libs = { { "kernel32.lib", "d3dcompiler.lib", "dxguid.lib" ; Config = "win64-*-*" } },

    Frameworks = {
        { "Cocoa" },
        { "Metal" },
        { "QuartzCore" },
        { "OpenGL" }
    },

    Depends = { "glslang", "spirv_tools" },

    IdeGenerationHints = { Msvc = { SolutionFolder = "Tools" } },
}

-----------------------------------------------------------------------------------------

local bgfx_defines = { 
        -- TODO: Don't duplicate
        { "BX_CONFIG_DEBUG=1", "_DEBUG" ; Config = { "*-*-debug" } },
        { "BX_CONFIG_DEBUG=0" ; Config = { "*-*-release" } },
        "__STDC_LIMIT_MACROS",
        "__STDC_FORMAT_MACROS",
        "__STDC_CONSTANT_MACROS",

        "BGFX_CONFIG_RENDERER_WEBGL=1",
        "BGFX_CONFIG_RENDERER_WEBGPU=0",
        "BGFX_CONFIG_RENDERER_GNM=0",
        {   
            "GLFW_EXPOSE_NATIVE_COCOA",
            "BGFX_CONFIG_MULTITHREADED=0",
            "BGFX_CONFIG_RENDERER_OPENGL=0", 
            "BGFX_CONFIG_RENDERER_VULKAN=0", 
            "BGFX_CONFIG_RENDERER_DIRECT3D11=0",
            "BGFX_CONFIG_RENDERER_DIRECT3D12=0",
            "BGFX_CONFIG_RENDERER_VULKAN=0",
            "BGFX_CONFIG_RENDERER_METAL=1" ; Config = "macos-*-*" 
        },
        {
            "BGFX_CONFIG_MULTITHREADED=1",
            "BGFX_CONFIG_RENDERER_VULKAN=1", 
            "BGFX_CONFIG_RENDERER_OPENGL=1", 
            "BGFX_CONFIG_RENDERER_DIRECT3D11=1",
            "BGFX_CONFIG_RENDERER_DIRECT3D12=1",
            "BGFX_CONFIG_RENDERER_METAL=0",
            "GLFW_EXPOSE_NATIVE_WIN32" ; Config = "win64-*-*" 
        },
        { 
            "BGFX_CONFIG_MULTITHREADED=1",
            "BGFX_CONFIG_RENDERER_VULKAN=1", 
            "BGFX_CONFIG_RENDERER_OPENGL=1", 
            "BGFX_CONFIG_RENDERER_DIRECT3D11=0", -- Enable when we have a solution for dx shaders
            "BGFX_CONFIG_RENDERER_DIRECT3D12=0", -- Enable when we have a solution for dx shaders
            "BGFX_CONFIG_RENDERER_METAL=0",
            "GLFW_EXPOSE_NATIVE_X11"; Config = "linux-*-*" 
        },
}

-----------------------------------------------------------------------------------------

StaticLibrary {
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
        BIMG_DIR .. "3rdparty/astc-codec/include",
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
        get_c_cpp_src(BIMG_DIR .. "/src"),
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

    IdeGenerationHints = { Msvc = { SolutionFolder = "External" } },
}

Default "bgfx_shaderc"
