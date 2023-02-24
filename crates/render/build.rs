fn main() {
    let mut build = cc::Build::new();
    let env = std::env::var("TARGET").unwrap();

    // windows includes
    if env.contains("windows") {
        build.include("c_cpp/bx/include/compat/msvc");
        build.include("c_cpp/bgfx/3rdparty/directx-headers/include/directx");
        build.flag("/Zc:__cplusplus");
    } else if env.contains("darwin") {
        // macOS includes
        build.include("c_cpp/bx/include/compat/osx");
        build.flag("-std=c++14");
    }

    // add shared include dirs
    build.include("c_cpp/bgfx/3rdparty/khronos");
    build.include("c_cpp/bgfx/3rdparty");
    build.include("c_cpp/bgfx/include");
    build.include("c_cpp/bx/include");
    build.include("c_cpp/bx/3rdparty");
    build.include("c_cpp/bimg/include");
    build.include("c_cpp/bimg/3rdparty");
    build.include("c_cpp/bimg/3rdparty/iqa/include");
    build.include("c_cpp/bimg/3rdparty/astc-codec/include");
    build.include("c_cpp/bimg/3rdparty/tinyexr/deps/miniz");

    // defines - Currently not supporting WebGPU, GNM and Vulkan
    // OS support:
    // Windows - DX11
    // macOS - Metal
    // Posix - OpenGL
    // In the future it would be good to make this configurable instead

    build.define("BGFX_CONFIG_RENDERER_WEBGPU", "0");
    build.define("BGFX_CONFIG_RENDERER_GNM", "0");

    // Make it optional to enable bgfx debug setting
    #[cfg(feature = "bgfx-debug")]
    {
        build.define("BX_CONFIG_DEBUG", "1");
    }

    #[cfg(not(feature = "bgfx-debug"))]
    {
        build.define("BX_CONFIG_DEBUG", "0");
    }

    // Don't include decode of ASTC to reduce code size and is unlikely a common use-case.
    build.define("BIMG_DECODE_ASTC", "0");

    // Optionally disable multi-threading
    #[cfg(feature = "bgfx-single-threaded")]
    {
        build.define("BGFX_CONFIG_MULTITHREADED", "0");
    }

    #[cfg(not(feature = "bgfx-single-threaded"))]
    {
        build.define("BGFX_CONFIG_MULTITHREADED", "1");
    }

    if env.contains("windows") {
        build.define("BGFX_CONFIG_RENDERER_VULKAN", "1");
        build.define("BGFX_CONFIG_RENDERER_DIRECT3D11", "1");
        build.define("BGFX_CONFIG_RENDERER_DIRECT3D12", "1");
        build.define("BGFX_CONFIG_RENDERER_OPENGL", "1");
        build.define("_WIN32", None);
        build.define("_HAS_EXCEPTIONS", "0");
        build.define("_SCL_SECURE", "0");
        build.define("_SECURE_SCL", "0");
        build.define("__STDC_LIMIT_MACROS", None);
        build.define("__STDC_FORMAT_MACROS", None);
        build.define("__STDC_CONSTANT_MACROS", None);
        build.define("_CRT_SECURE_NO_WARNINGS", None);
        build.define("_CRT_SECURE_NO_DEPRECATE", None);
        build.warnings(false);
    } else if env.contains("darwin") {
        build.define("BGFX_CONFIG_RENDERER_VULKAN", "0");
        build.define("BGFX_CONFIG_RENDERER_METAL", "1");
    } else if env.contains("android") {
        build.define("BGFX_CONFIG_RENDERER_VULKAN", "1");
        build.define("BGFX_CONFIG_RENDERER_OPENGLES", "1");
    } else {
        build.define("BGFX_CONFIG_RENDERER_VULKAN", "1");
        build.define("BGFX_CONFIG_RENDERER_OPENGL", "1");
    }

    // sources
    build.file("c_cpp/bx/src/amalgamated.cpp");
    build.file("c_cpp/bimg/src/image.cpp");
    build.file("c_cpp/bimg/src/image_cubemap_filter.cpp");
    build.file("c_cpp/bimg/src/image_decode.cpp");
    build.file("c_cpp/bimg/src/image_gnf.cpp");
    build.file("c_cpp/bgfx/src/bgfx.cpp");
    build.file("c_cpp/bgfx/src/vertexlayout.cpp");
    //build.file("c_cpp/bgfx/src/debug_renderdoc.cpp");
    build.file("c_cpp/bgfx/src/topology.cpp");
    build.file("c_cpp/bgfx/src/shader.cpp");
    build.file("c_cpp/bgfx/src/shader_dxbc.cpp");
    build.file("c_cpp/bgfx/src/renderer_agc.cpp");
    build.file("c_cpp/bgfx/src/renderer_gnm.cpp");
    build.file("c_cpp/bgfx/src/renderer_webgpu.cpp");
    build.file("c_cpp/bgfx/src/renderer_nvn.cpp");
    build.file("c_cpp/bgfx/src/renderer_gl.cpp");
    build.file("c_cpp/bgfx/src/renderer_vk.cpp");
    build.file("c_cpp/bgfx/src/renderer_noop.cpp");
    build.file("c_cpp/bgfx/src/renderer_d3d9.cpp");
    build.file("c_cpp/bgfx/src/renderer_d3d11.cpp");
    build.file("c_cpp/bgfx/src/renderer_d3d12.cpp");

    if env.contains("windows") {
        build.file("c_cpp/bgfx/src/glcontext_wgl.cpp");
        build.file("c_cpp/bgfx/src/nvapi.cpp");
        build.file("c_cpp/bgfx/src/dxgi.cpp");
        build.file("c_cpp/bgfx/src/shader_dx9bc.cpp");
        build.file("c_cpp/bgfx/src/shader_spirv.cpp");
    } else if env.contains("darwin") {
        build.file("c_cpp/bgfx/src/glcontext_nsgl.mm");
        build.file("c_cpp/bgfx/src/renderer_mtl.mm");
    } else if env.contains("android") {
        build.file("c_cpp/bgfx/src/glcontext_egl.cpp");
    } else {
        build.file("c_cpp/bgfx/src/glcontext_glx.cpp");
        build.cpp_link_stdlib("stdc++");
    }

    build.compile("bgfx_sys");

    // linker stuff
    if env.contains("windows") {
        // todo fixme
    } else if env.contains("darwin") {
        println!("cargo:rustc-link-lib=framework=Metal");
        println!("cargo:rustc-link-lib=framework=MetalKit");
        println!("cargo:rustc-link-lib=c++");
    } else if env.contains("android") {
        println!("cargo:rustc-link-lib=c++_shared");
        println!("cargo:rustc-link-lib=GLESv1_CM");
        println!("cargo:rustc-link-lib=GLESv2");
        println!("cargo:rustc-link-lib=EGL");
    } else {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=stdc++");
        println!("cargo:rustc-link-lib=GL");
        println!("cargo:rustc-link-lib=X11");
    }
}

