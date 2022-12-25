use std::path::Path;

fn add_source_files(path: &str, build: &mut cc::Build, filenames: &[&str]) {
    for file in filenames {
        let path = Path::new(path).join(file);
        build.file(path);
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

/// Build the bgfx code
fn build_bgfx(root: &str, target_os: &str) {
    let mut build = cc::Build::new();

    println!("cargo:rerun-if-changed={}", root);

    add_includes(
        &mut build,
        root,
        &[
            "bgfx/3rdparty/khronos",
            "bgfx/3rdparty",
            "bgfx/include",
            "bx/include",
            "bx/3rdparty",
            "bimg/include",
            "bimg/3rdparty",
            "bimg/3rdparty/iqa/include",
            "bimg/3rdparty/astc-codec/include",
            "bimg/3rdparty/tinyexr/deps/miniz",
        ],
    );

    add_sources(
        &mut build,
        root,
        &[
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
            "bgfx/src/renderer_d3d12.cpp",
        ],
    );

    // defines - Currently not supporting WebGPU, GNM and Vulkan
    // OS support:
    // Windows - DX11
    // macOS - Metal
    // Posix - OpenGL
    // In the future it would be good to make this configurable instead

    build.define("BGFX_CONFIG_RENDERER_WEBGPU", "0");
    build.define("BGFX_CONFIG_RENDERER_GNM", "0");
    build.define("BIMG_DECODE_ASTC", "0");
    build.define("__STDC_CONSTANT_MACROS", None);
    build.define("__STDC_LIMIT_MACROS", None);
    build.define("__STDC_FORMAT_MACROS", None);
    build.define("BGFX_SHARED_LIB_BUILD", "0");
    build.define("BIMG_DECODE_ASTC", "0");

    build.define("BX_CONFIG_DEBUG", "0");

    match target_os {
        "linux" => {
            build.flag("-g");
            build.flag("-msse2");
            build.flag("-std=c++14");

            build.define("BGFX_CONFIG_RENDERER_VULKAN", "1");
            build.define("BGFX_CONFIG_RENDERER_OPENGL", "1");

            add_sources(&mut build, root, &["bgfx/src/glcontext_glx.cpp"]);
            build.cpp_link_stdlib("stdc++");

            println!("cargo:rustc-link-lib=pthread");
            println!("cargo:rustc-link-lib=stdc++");
            println!("cargo:rustc-link-lib=GL");
            println!("cargo:rustc-link-lib=X11");
        }

        "windows" => {
            build.define("BGFX_CONFIG_RENDERER_VULKAN", "1");
            build.define("BGFX_CONFIG_RENDERER_DIRECT3D11", "1");
            build.define("_WIN32", None);
            build.define("_HAS_EXCEPTIONS", "0");
            build.define("_SCL_SECURE", "0");
            build.define("_SECURE_SCL", "0");
            build.define("_CRT_SECURE_NO_WARNINGS", None);
            build.define("_CRT_SECURE_NO_DEPRECATE", None);
            build.warnings(false);

            add_includes(
                &mut build,
                root,
                &["bx/include/compat/msvc", "bgfx/3rdparty/dxsdk/include"],
            );

            add_sources(
                &mut build,
                root,
                &[
                    "bgfx/src/glcontext_wgl.cpp",
                    "bgfx/src/nvapi.cpp",
                    "bgfx/src/dxgi.cpp",
                    "bgfx/src/shader_dx9bc.cpp",
                    "bgfx/src/shader_spirv.cpp",
                ],
            );
        }

        "macos" => {
            build.define("BGFX_CONFIG_RENDERER_VULKAN", "0");
            build.define("BGFX_CONFIG_RENDERER_METAL", "1");

            add_sources(
                &mut build,
                root,
                &["bgfx/src/glcontext_nsgl.mm", "bgfx/src/renderer_mtl.mm"],
            );

            add_includes(&mut build, root, &["bx/include/compat/osx"]);

            println!("cargo:rustc-link-lib=framework=Metal");
            println!("cargo:rustc-link-lib=framework=MetalKit");
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("bgfx");
}

/// Build the glfw code
fn build_glfw(root: &str, target_os: &str) {
    let mut build = cc::Build::new();

    add_includes(&mut build, root, &["src", "include"]);

    add_sources(
        &mut build, 
        root, 
        &[
            "src/window.c",
            "src/context.c",
            "src/init.c",
            "src/input.c",
            "src/monitor.c",
            "src/vulkan.c",
            "src/osmesa_context.c",
            "src/egl_context.c"
        ],
    );

    match target_os {
        "linux" => {
            build.define("GLFW_EXPOSE_NATIVE_X11", None);
            build.define("_GLFW_X11", None);
            build.define("_GLFW_GFX", None);
            build.define("LINUX", None);
            build.flag("-Wno-unused-parameter");
            build.flag("-Wno-missing-field-initializers");
            build.flag("-Wno-sign-compare");

            add_source_files(
                root, 
                &mut build, 
                &[
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
                    "src/xkb_unicode.c"
                ],
            );
        }

        "windows" => {
            build.define("GLFW_EXPOSE_NATIVE_WIN32", None);
            build.define("_GLFW_WIN32", None);
            build.define("WIN32", None);

            add_source_files(
                root, 
                &mut build, 
                &[
                    "src/wgl_context.c",
                    "src/win32_init.c",
                    "src/win32_joystick.c",
                    "src/win32_monitor.c",
                    "src/win32_thread.c",
                    "src/win32_time.c",
                    "src/win32_window.c"
                ],
            );
        }

        "macos" => {
            build.define("GLFW_EXPOSE_NATIVE_COCOA", None);
            build.define("_GLFW_COCOA", None);
            build.define("MACOSX", None);

            add_source_files(
                root, 
                &mut build, 
                &[
                    "src/cocoa_init.m",
                    "src/cocoa_joystick.m",
                    "src/cocoa_monitor.m",
                    "src/cocoa_time.c",
                    "src/cocoa_window.m",
                    "src/posix_thread.c",
                    "src/nsgl_context.h",
                    "src/nsgl_context.m"
                ],
            );
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("glfw");
}

fn main() {
    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

    build_bgfx("external", target_os);
    build_glfw("external/glfw", target_os);
}

