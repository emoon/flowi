use std::path::Path;


fn add_source_file(path: &str, filename: &str, build: &mut cc::Build) {
    let path = Path::new(path).join(filename);
    build.file(path);
}

fn add_include_dir(path: &str, filename: &str, build: &mut cc::Build) {
    let path = Path::new(path).join(filename);
    build.include(path);
}

/// Build the bgfx code
fn build_bgfx(source_dir: &str, env: &str, build: &mut cc::Build) {
    // windows includes
    if env.contains("windows") {
        add_include_dir(source_dir, "bx/include/compat/msvc", build);
        add_include_dir(source_dir, "bgfx/3rdparty/directx-headers/include/directx", build);
        build.flag("/Zc:__cplusplus");
    } else if env.contains("darwin") {
        // macOS includes
        add_include_dir(source_dir, "bx/include/compat/osx", build);
        build.flag("-std=c++14");
    }

    // add shared include dirs
    add_include_dir(source_dir, "bgfx/3rdparty/khronos", build);
    add_include_dir(source_dir, "bgfx/3rdparty", build);
    add_include_dir(source_dir, "bgfx/include", build);
    add_include_dir(source_dir, "bx/include", build);
    add_include_dir(source_dir, "bx/3rdparty", build);
    add_include_dir(source_dir, "bimg/include", build);
    add_include_dir(source_dir, "bimg/3rdparty", build);
    add_include_dir(source_dir, "bimg/3rdparty/iqa/include", build);
    add_include_dir(source_dir, "bimg/3rdparty/astc-codec/include", build);
    add_include_dir(source_dir, "bimg/3rdparty/tinyexr/deps/miniz", build);

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
    add_source_file(source_dir, "bx/src/amalgamated.cpp", build);
    add_source_file(source_dir, "bimg/src/image.cpp", build);
    add_source_file(source_dir, "bimg/src/image_cubemap_filter.cpp", build);
    add_source_file(source_dir, "bimg/src/image_decode.cpp", build);
    add_source_file(source_dir, "bimg/src/image_gnf.cpp", build);
    add_source_file(source_dir, "bgfx/src/bgfx.cpp", build);
    add_source_file(source_dir, "bgfx/src/vertexlayout.cpp", build);
    add_source_file(source_dir, "bgfx/src/debug_renderdoc.cpp", build);
    add_source_file(source_dir, "bgfx/src/topology.cpp", build);
    add_source_file(source_dir, "bgfx/src/shader.cpp", build);
    add_source_file(source_dir, "bgfx/src/shader_dxbc.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_agc.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_gnm.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_webgpu.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_nvn.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_gl.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_vk.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_noop.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_d3d9.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_d3d11.cpp", build);
    add_source_file(source_dir, "bgfx/src/renderer_d3d12.cpp", build);

    if env.contains("windows") {
        add_source_file(source_dir, "bgfx/src/glcontext_wgl.cpp", build);
        add_source_file(source_dir, "bgfx/src/nvapi.cpp", build);
        add_source_file(source_dir, "bgfx/src/dxgi.cpp", build);
        add_source_file(source_dir, "bgfx/src/shader_dx9bc.cpp", build);
        add_source_file(source_dir, "bgfx/src/shader_spirv.cpp", build);
    } else if env.contains("darwin") {
        add_source_file(source_dir, "bgfx/src/glcontext_nsgl.mm", build);
        add_source_file(source_dir, "bgfx/src/renderer_mtl.mm", build);
    } else if env.contains("android") {
        add_source_file(source_dir, "bgfx/src/glcontext_egl.cpp", build);
    } else {
        add_source_file(source_dir, "bgfx/src/glcontext_glx.cpp", build);
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

fn main() {
    let mut build = cc::Build::new();
    let env = std::env::var("TARGET").unwrap();
    build_bgfx("external", &env, &mut build);
}

