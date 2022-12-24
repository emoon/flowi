use std::path::Path;

fn add_source_files(path: &str, build: &mut cc::Build, filenames: &[&str]) {
    for file in filenames {
        let path = Path::new(path).join(file);
        build.file(path);
    }
}

fn add_include_dirs(base_path: &str, build: &mut cc::Build, paths: &[&str]) {
    for path in paths {
        let path = Path::new(base_path).join(path);
        build.include(path);
    }
}

fn add_sources(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = Path::new(root);
    build.files(files.iter().map(|src| root.join(src)));
}

fn add_includes(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = Path::new(root);
    build.includes(files.iter().map(|src| root.join(src)));
}

fn add_defines<'a, V: Into<Option<&'a str>> + Copy>(build: &mut cc::Build, defines: &[(&str, V)]) {
    for define in defines {
        build.define(define.0, define.1);
    }
}

/// Build the bgfx code
fn build_bgfx(source_dir: &str, env: &str, build: &mut cc::Build) {
    // windows includes
    if env.contains("windows") {
        add_include_dirs(source_dir, build, &[
            "bx/include/compat/msvc", 
            "bgfx/3rdparty/directx-headers/include/directx"]);
        build.flag("/Zc:__cplusplus");
    } else if env.contains("darwin") {
        // macOS includes
        add_include_dirs(source_dir, build, &["bx/include/compat/osx"]);
        build.flag("-std=c++14");
    }

    // add shared include dirs
    add_include_dirs(source_dir, build, &[
        "bgfx/3rdparty/khronos", 
        "bgfx/3rdparty",
        "bgfx/include",
        "bx/include",
        "bx/3rdparty",
        "bimg/include",
        "bimg/3rdparty",
        "bimg/3rdparty/iqa/include",
        "bimg/3rdparty/astc-codec/include",
        "bimg/3rdparty/tinyexr/deps/miniz"]);

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
        add_defines(build, &[
            ("BGFX_CONFIG_RENDERER_VULKAN", Some("1")),
            ("BGFX_CONFIG_RENDERER_VULKAN", Some("1")),
            ("BGFX_CONFIG_RENDERER_DIRECT3D11", Some("1")),
            ("BGFX_CONFIG_RENDERER_DIRECT3D12", Some("1")),
            ("BGFX_CONFIG_RENDERER_OPENGL", Some("1")),
            ("_HAS_EXCEPTIONS", Some("0")),
            ("_SCL_SECURE", Some("0")),
            ("_SECURE_SCL", Some("0")),
            ("__STDC_LIMIT_MACROS", None),
            ("__STDC_FORMAT_MACROS", None),
            ("__STDC_CONSTANT_MACROS", None),
            ("_CRT_SECURE_NO_WARNINGS", None),
            ("_CRT_SECURE_NO_DEPRECATE", None),
        ]);
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
    add_source_files(source_dir, build, &[
        "bx/src/amalgamated.cpp",
        "bimg/src/image.cpp",
        "bimg/src/image_cubemap_filter.cpp",
        "bimg/src/image_decode.cpp",
        "bimg/src/image_gnf.cpp",
        "bgfx/src/bgfx.cpp",
        "bgfx/src/vertexlayout.cpp",
        "bgfx/src/debug_renderdoc.cpp",
        "bgfx/src/topology.cpp",
        "bgfx/src/shader.cpp",
        "bgfx/src/shader_dxbc.cpp",
        "bgfx/src/renderer_agc.cpp",
        "bgfx/src/renderer_gnm.cpp",
        "bgfx/src/renderer_webgpu.cpp",
        "bgfx/src/renderer_nvn.cpp",
        "bgfx/src/renderer_gl.cpp",
        "bgfx/src/renderer_vk.cpp",
        "bgfx/src/renderer_noop.cpp",
        "bgfx/src/renderer_d3d9.cpp",
        "bgfx/src/renderer_d3d11.cpp",
        "bgfx/src/renderer_d3d12.cpp"]);

    if env.contains("windows") {
        add_source_files(source_dir, build, &[
            "bgfx/src/glcontext_wgl.cpp",
            "bgfx/src/nvapi.cpp",
            "bgfx/src/dxgi.cpp",
            "bgfx/src/shader_dx9bc.cpp",
            "bgfx/src/shader_spirv.cpp"]);

    } else if env.contains("darwin") {
        add_source_files(source_dir, build, &[
            "bgfx/src/glcontext_nsgl.mm",
            "bgfx/src/renderer_mtl.mm"]);
    } else if env.contains("android") {
        add_source_files(source_dir, build, &["bgfx/src/glcontext_egl.cpp"]);
    } else {
        build.flag("-g");
        build.flag("-msse2");
        build.flag("-std=c++14");

        add_source_files(source_dir, build, &["bgfx/src/glcontext_glx.cpp"]);
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

/// Build the glfw code
fn build_glfw(source_dir: &str, env: &str) {
    let mut build = cc::Build::new();

    add_includes(&mut build, source_dir, &["src", "include"]);
    add_sources(&mut build, source_dir, &[
        "src/window.c",
        "src/context.c",
        "src/init.c",
        "src/input.c",
        "src/monitor.c",
        "src/vulkan.c",
        "src/osmesa_context.c",
        "src/egl_context.c"]);
    
    if env.contains("darwin") {
        add_defines(&mut build, &[
            ("GLFW_EXPOSE_NATIVE_COCOA", None),
            ("_GLFW_COCOA", None), 
            ("MACOSX", None)]);
        add_source_files(source_dir, &mut build, &[
            "src/cocoa_init.m",
            "src/cocoa_joystick.m",
            "src/cocoa_monitor.m",
            "src/cocoa_time.c",
            "src/cocoa_window.m",
            "src/posix_thread.c",
            "src/nsgl_context.h",
            "src/nsgl_context.m"]);
    } else if env.contains("windows") {
        add_defines(&mut build, &[
            ("GLFW_EXPOSE_NATIVE_WIN32", None),
            ("_GLFW_WIN32", None), 
            ("WIN32", None)]);
        add_source_files(source_dir, &mut build, &[
            "src/wgl_context.c",
            "src/win32_init.c",
            "src/win32_joystick.c",
            "src/win32_monitor.c",
            "src/win32_thread.c",
            "src/win32_time.c",
            "src/win32_window.c"]);
    } else {
        build.define("GLFW_EXPOSE_NATIVE_X11", None);
        build.define("_GLFW_X11", None);
        build.define("_GLFW_GFX", None);
        build.define("LINUX", None);
        build.flag("-Wno-unused-parameter");
        build.flag("-Wno-missing-field-initializers");
        build.flag("-Wno-sign-compare");

        add_source_files(source_dir, &mut build, &[
            "src/glx_context.c",
            //"src/wl_init.c",
            //"src/wl_monitor.c",
            //"src/wl_window.c",
            "src/x11_init.c",
            "src/x11_monitor.c",
            "src/x11_window.c",
            "src/linux_joystick.c",
            "src/posix_thread.c",
            "src/posix_time.c",
            "src/xkb_unicode.c"]);
    }

    build.compile("glfw");
}

fn main() {
    let mut build = cc::Build::new();
    let env = std::env::var("TARGET").unwrap();
    build_bgfx("external", &env, &mut build);
    build_glfw("external/glfw", &env);
}

