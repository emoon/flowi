use std::path::Path;

fn add_sources(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = Path::new(root);
    build.files(files.iter().map(|src| root.join(src)));
}

fn add_includes(build: &mut cc::Build, root: &str, files: &[&str]) {
    build.includes(files.iter().map(|src| format!("{}/{}", root, src)));
}

fn build_freetype2() {
    let mut build = cc::Build::new();

    build
        .cpp(true)
        .warnings(false)
        .include(".")
        .include("../../external/freetype2/include")
        .define("FT2_BUILD_LIBRARY", None);

    add_sources(
        &mut build,
        "../../external/freetype2/src",
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

fn main() {
    let mut build = cc::Build::new();

    let bgfx_extern = "../../external";
    let glfw_extern = "../../external/glfw";
    let dear_imgui_extern = "../../external/dear_imgui";
    let flowi_src = "../../src";
    let flowi_root = "../..";

    let (root, glfw_root) = if Path::new(bgfx_extern).exists() {
        (bgfx_extern, glfw_extern)
    } else {
        ("", "")
    };

    println!("cargo:rerun-if-changed={}", root);
    println!("cargo:rerun-if-changed={}", flowi_src);

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

    //#[cfg(feature = "bgfx-debug")]
    {
        build.define("BX_CONFIG_DEBUG", "0");
        //build.define("BGFX_CONFIG_DEBUG", "0");
    }

    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

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
            
            println!("cargo:rustc-link-lib=opengl32");
            println!("cargo:rustc-link-lib=kernel32");
            println!("cargo:rustc-link-lib=user32");
            println!("cargo:rustc-link-lib=gdi32");
            println!("cargo:rustc-link-lib=shell32");
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

    // glfw

    let mut build = cc::Build::new();

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
            "src/osmesa_context.c",
            "src/egl_context.c",
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
                    "src/nsgl_context.h",
                    "src/nsgl_context.m",
                ],
            );
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("glfw");

    // Build flowi
    let mut build = cc::Build::new();

    build.define("BX_CONFIG_DEBUG", "0");

    add_includes(&mut build, glfw_root, &["include"]);
    add_includes(
        &mut build,
        root,
        &[
            "bx/include",
            "bgfx/include",
            "dear-imgui",
            "",
            "nanosvg",
            "stb",
        ],
    );
    add_includes(&mut build, flowi_root, &["include", "src", "external/freetype2/include"]);

    add_sources(
        &mut build,
        flowi_root,
        &[
            "src/application.cpp",
            "src/area.c",
            "src/array.c",
            "src/atlas.c",
            "src/command_buffer.c",
            "src/font.cpp",
            "src/flowi.cpp",
            "src/glfw_input.cpp",
            "src/handles.c",
            "src/image.c",
            "src/imgui_wrap.cpp",
            "src/io.c",
            "src/layer.c",
            "src/linear_allocator.c",
            "src/primitive_rect.c",
            "src/string_allocator.c",
            "src/style.cpp",
            "src/text.c",
            "src/vertex_allocator.c",
            "external/dear-imgui/imgui.cpp",
            "external/dear-imgui/imgui_draw.cpp",
            "external/dear-imgui/imgui_tables.cpp",
            "external/dear-imgui/imgui_widgets.cpp",
            "external/dear-imgui/misc/freetype/imgui_freetype.cpp",
            "external/nanosvg/nanosvg.c",
            "external/stb/stb.c",
        ],
    );

    match target_os {
        "linux" => {
            build.define("GLFW_EXPOSE_NATIVE_X11", None);
        }

        "windows" => {
            add_includes(
                &mut build,
                root,
                &["bx/include/compat/msvc", "bgfx/3rdparty/dxsdk/include"],
            );

            build.define("GLFW_EXPOSE_NATIVE_WIN32", None);
        }

        "macos" => {
            build.define("GLFW_EXPOSE_NATIVE_COCOA", None);
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("flowi");

    build_freetype2();
}
