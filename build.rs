use std::path::Path;

fn add_sources(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = Path::new(root);
    build.files(files.iter().map(|src| root.join(src)));
}

fn add_includes(build: &mut cc::Build, root: &str, files: &[&str]) {
    build.includes(files.iter().map(|src| format!("{}/{}", root, src)));
}

#[cfg(feature = "static")]
fn build_freetype2(target_os: &str) {
    let mut build = cc::Build::new();

    println!("cargo:rerun-if-changed=external/freetype2");

    build
        .warnings(false)
        .include(".")
        .include("external/freetype2/include")
        .define("FT2_BUILD_LIBRARY", None);

    match target_os {
        "linux" => {
            build.flag("-Wno-enum-compare");
        }

        "macos" => {
            build.flag("-Wno-enum-compare");
        }

        _ => {}
    }

    add_sources(
        &mut build,
        "external/freetype2/src",
        &[
            "autofit/autofit.c",
            "base/ftbase.c",
            "base/ftbbox.c",
            "base/ftbdf.c",
            "base/ftbitmap.c",
            "base/ftcid.c",
            "base/ftdebug.c",
            "base/ftfstype.c",
            "base/ftgasp.c",
            "base/ftglyph.c",
            "base/ftgxval.c",
            "base/ftinit.c",
            "base/ftmm.c",
            "base/ftotval.c",
            "base/ftpatent.c",
            "base/ftpfr.c",
            "base/ftstroke.c",
            "base/ftsynth.c",
            "base/ftsystem.c",
            "base/fttype1.c",
            "base/ftwinfnt.c",
            "bdf/bdf.c",
            "bzip2/ftbzip2.c",
            "cache/ftcache.c",
            "cff/cff.c",
            "cid/type1cid.c",
            "gzip/ftgzip.c",
            "lzw/ftlzw.c",
            "pcf/pcf.c",
            "pfr/pfr.c",
            "psaux/psaux.c",
            "pshinter/pshinter.c",
            "psnames/psnames.c",
            "raster/raster.c",
            "sdf/sdf.c",
            "svg/svg.c",
            "sfnt/sfnt.c",
            "smooth/smooth.c",
            "truetype/truetype.c",
            "type1/type1.c",
            "type42/type42.c",
            "winfonts/winfnt.c",
        ],
    );

    build.compile("freetype2");
}

#[cfg(feature = "static")]
fn build_dear_imgui(target_os: &str) {
    let mut build = cc::Build::new();

    build.cpp(true);

    println!("cargo:rerun-if-changed=external/dear-imgui");

    match target_os {
        "linux" => {
            build.flag("-std=c++11");
        }

        "macos" => {
            build.flag("-std=c++11");
        }

        _ => (),
    }

    add_includes(&mut build, "external", &["dear-imgui", "freetype2/include"]);

    add_sources(
        &mut build,
        "external/dear-imgui",
        &[
            "imgui.cpp",
            "imgui_draw.cpp",
            "imgui_tables.cpp",
            "imgui_widgets.cpp",
            "misc/freetype/imgui_freetype.cpp",
        ],
    );

    build.compile("dear-imgui");
}

#[cfg(feature = "static")]
fn build_ui(target_os: &str) {
    // Build flowi
    let mut build = cc::Build::new();
    let mut build_c = cc::Build::new();

    println!("cargo:rerun-if-changed=c_cpp");

    build.define("BX_CONFIG_DEBUG", "0");

    match target_os {
        "linux" => {
            build.flag("-std=c++11");
        }

        "macos" => {
            build.flag("-std=c++11");
        }

        _ => (),
    }

    add_includes(
        &mut build,
        ".",
        &[
            "langs/c_cpp/include",
            "external/glfw/include",
            "external/bgfx/include",
            "external/bx/include",
            "external",
            "external/dear-imgui",
            "external/freetype2/include",
        ],
    );

    add_includes(
        &mut build_c,
        ".",
        &[
            "langs/c_cpp/include",
            "external/glfw/include",
            "external/bgfx/include",
            "external/bx/include",
            "external",
            "external/nanosvg",
            "external/dear-imgui",
            "external/stb",
            "external/freetype2/include",
        ],
    );

    add_sources(
        &mut build_c,
        "c_cpp",
        &[
            "image.c",
            "area.c",
            "array.c",
            "atlas.c",
            "command_buffer.c",
            "handles.c",
            //"image.c",
            "io.c",
            "layer.c",
            "linear_allocator.c",
            "primitive_rect.c",
            "string_allocator.c",
            "text.c",
            "vertex_allocator.c",
            "../external/nanosvg/nanosvg.c",
            "../external/stb/stb.c",
        ],
    );

    add_sources(
        &mut build,
        "c_cpp",
        &[
            "application.cpp",
            "flowi.cpp",
            "font.cpp",
            "glfw_input.cpp",
            //"image.c",
            "imgui_wrap.cpp",
            "style.cpp",
        ],
    );

    match target_os {
        "linux" => {
            build.define("GLFW_EXPOSE_NATIVE_X11", None);
        }

        "windows" => {
            add_includes(
                &mut build,
                ".",
                &[
                    "external/bx/include/compat/msvc",
                    "external/bgfx/3rdparty/dxsdk/include",
                ],
            );

            build.define("GLFW_EXPOSE_NATIVE_WIN32", None);
        }

        "macos" => {
            build.define("GLFW_EXPOSE_NATIVE_COCOA", None);
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("ui");
    build_c.compile("ui-c");
}

#[cfg(feature = "static")]
fn build_bgfx(_target_os: &str) {
    let mut build = cc::Build::new();
    let env = std::env::var("TARGET").unwrap();

    println!("cargo:rerun-if-changed=external/bgfx");
    println!("cargo:rerun-if-changed=external/bx");
    println!("cargo:rerun-if-changed=external/bimg");

    // windows includes
    if env.contains("windows") {
        build.include("external/bx/include/compat/msvc");
        build.include("external/bgfx/3rdparty/directx-headers/include/directx");
        build.flag("/Zc:__cplusplus");
    } else if env.contains("darwin") {
        // macOS includes
        build.include("external/bx/include/compat/osx");
        build.flag("-std=c++14");
    }

    // add shared include dirs
    build.include("external/bgfx/3rdparty/khronos");
    build.include("external/bgfx/3rdparty");
    build.include("external/bgfx/include");
    build.include("external/bx/include");
    build.include("external/bx/3rdparty");
    build.include("external/bimg/include");
    build.include("external/bimg/3rdparty");
    build.include("external/bimg/3rdparty/iqa/include");
    build.include("external/bimg/3rdparty/astc-codec/include");
    build.include("external/bimg/3rdparty/tinyexr/deps/miniz");

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
    build.file("external/bx/src/amalgamated.cpp");
    build.file("external/bimg/src/image.cpp");
    build.file("external/bimg/src/image_cubemap_filter.cpp");
    build.file("external/bimg/src/image_decode.cpp");
    build.file("external/bimg/src/image_gnf.cpp");
    build.file("external/bgfx/src/bgfx.cpp");
    build.file("external/bgfx/src/vertexlayout.cpp");
    build.file("external/bgfx/src/debug_renderdoc.cpp");
    build.file("external/bgfx/src/topology.cpp");
    build.file("external/bgfx/src/shader.cpp");
    build.file("external/bgfx/src/shader_dxbc.cpp");
    build.file("external/bgfx/src/renderer_agc.cpp");
    build.file("external/bgfx/src/renderer_gnm.cpp");
    build.file("external/bgfx/src/renderer_webgpu.cpp");
    build.file("external/bgfx/src/renderer_nvn.cpp");
    build.file("external/bgfx/src/renderer_gl.cpp");
    build.file("external/bgfx/src/renderer_vk.cpp");
    build.file("external/bgfx/src/renderer_noop.cpp");
    build.file("external/bgfx/src/renderer_d3d9.cpp");
    build.file("external/bgfx/src/renderer_d3d11.cpp");
    build.file("external/bgfx/src/renderer_d3d12.cpp");

    if env.contains("windows") {
        build.file("external/bgfx/src/glcontext_wgl.cpp");
        build.file("external/bgfx/src/nvapi.cpp");
        build.file("external/bgfx/src/dxgi.cpp");
        build.file("external/bgfx/src/shader_dx9bc.cpp");
        build.file("external/bgfx/src/shader_spirv.cpp");
    } else if env.contains("darwin") {
        build.file("external/bgfx/src/glcontext_nsgl.mm");
        build.file("external/bgfx/src/renderer_mtl.mm");
    } else if env.contains("android") {
        build.file("external/bgfx/src/glcontext_egl.cpp");
    } else {
        build.file("external/bgfx/src/glcontext_glx.cpp");
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

#[cfg(feature = "static")]
fn build_glfw(target_os: &str) {
    let mut build = cc::Build::new();
    let glfw_root = "external/glfw";

    println!("cargo:rerun-if-changed=external/glfw");

    add_includes(&mut build, glfw_root, &["src", "include"]);

    add_sources(
        &mut build,
        glfw_root,
        &[
            "src/window.c",
            "src/context.c",
            "src/init.c",
            "src/input.c",
            "src/monitor.c",
            "src/vulkan.c",
            "src/platform.c",
            "src/osmesa_context.c",
            "src/egl_context.c",
            "src/null_init.c",
            "src/null_window.c",
            "src/null_monitor.c",
            "src/null_joystick.c",
            //"src/null_platform.c",
        ],
    );

    match target_os {
        "linux" => {
            build.define("_GLFW_X11", None);
            build.define("_GLFW_GFX", None);
            build.define("LINUX", None);
            build.flag("-Wno-unused-parameter");
            build.flag("-Wno-missing-field-initializers");
            build.flag("-Wno-sign-compare");

            add_sources(
                &mut build,
                glfw_root,
                &[
                    "src/glx_context.c",
                    // "src/wl_init.c",
                    // "src/wl_monitor.c",
                    // "src/wl_window.c",
                    "src/x11_init.c",
                    "src/x11_monitor.c",
                    "src/x11_window.c",
                    "src/linux_joystick.c",
                    "src/posix_thread.c",
                    "src/posix_time.c",
                    "src/posix_module.c",
                    "src/posix_poll.c",
                    "src/xkb_unicode.c",
                ],
            );
        }

        "windows" => {
            build.define("_GLFW_WIN32", None);
            build.define("_GLFW_WGL", None);
            build.define("WIN32", None);

            add_sources(
                &mut build,
                glfw_root,
                &[
                    "src/wgl_context.c",
                    "src/win32_init.c",
                    "src/win32_joystick.c",
                    "src/win32_monitor.c",
                    "src/win32_thread.c",
                    "src/win32_time.c",
                    "src/win32_window.c",
                ],
            );
        }

        "macos" => {
            build.define("_GLFW_COCOA", None);
            build.define("MACOSX", None);
            build.flag("-Wno-unused-parameter");

            add_sources(
                &mut build,
                glfw_root,
                &[
                    "src/cocoa_init.m",
                    "src/cocoa_joystick.m",
                    "src/cocoa_monitor.m",
                    "src/cocoa_time.c",
                    "src/cocoa_window.m",
                    "src/posix_thread.c",
                    "src/nsgl_context.m",
                ],
            );
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("glfw");
}

// When building the dynamic build we don't compile any of the C/C++ code

#[cfg(feature = "dynamic")]
fn build_freetype2(_target_os: &str) {}

#[cfg(feature = "dynamic")]
fn build_dear_imgui(_target_os: &str) {}

#[cfg(feature = "dynamic")]
fn build_ui(_target_os: &str) {}

#[cfg(feature = "dynamic")]
fn build_bgfx(_target_os: &str) {}

#[cfg(feature = "dynamic")]
fn build_glfw(_target_os: &str) {}

fn main() {
    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

    build_freetype2(target_os);
    build_dear_imgui(target_os);
    build_ui(target_os);
    build_bgfx(target_os);
    build_glfw(target_os);
}
