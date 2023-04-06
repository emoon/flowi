#[cfg(feature = "tundra")]
use std::process::Command;

#[cfg(feature = "static")]
fn add_sources(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = std::Path::new(root);
    build.files(files.iter().map(|src| root.join(src)));
}

#[cfg(feature = "static")]
fn add_includes(build: &mut cc::Build, root: &str, files: &[&str]) {
    build.includes(files.iter().map(|src| format!("{}/{}", root, src)));
}

/*
#[cfg(feature = "static")]
fn build_cc(target_os: &str) {
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
            "external/nanosvg",
            "external/dear-imgui",
            "external/stb",
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
            "io.cpp",
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
*/

#[cfg(target_os = "macos")]
fn tundra_target() -> &'static str {
    "macos-clang-debug"
}

#[cfg(target_os = "linux")]
fn tundra_target() -> &'static str {
    "linux-clang-debug"
}

#[cfg(feature = "tundra")]
fn build_tundra(target_os: &str) {
    let output = Command::new("tundra2")
        //.arg("linux-clang-debug")
        .arg(tundra_target())
        .output()
        .expect("tundra2 failed");

    if !output.status.success() {
        panic!(
            "tundra2 failed:\n {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }

    let target_dir = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR not specified");
    //let tundra_dir = format!("{}/t2-output/linux-clang-debug-default", target_dir);
    //println!("cargo:rustc-link-lib=stdc++");
    //println!("cargo:rustc-link-lib=GL");
    //println!("cargo:rustc-link-lib=X11");
    //
    /*

    println!("cargo:rustc-link-search=native={}", tundra_dir);
    //println!("cargo:rustc-link-lib=static=bgfx");
    println!("cargo:rustc-link-lib=static=ui");
    println!("cargo:rustc-link-lib=static=freetype2");
    //println!("cargo:rustc-link-lib=static=imgui");
    println!("cargo:rustc-link-lib=static=glfw");
    println!("cargo:rustc-link-lib=pthread");
    */


    match target_os {
        "linux" => {
            let tundra_dir = format!("{}/t2-output/linux-clang-debug-default", target_dir);
            println!("cargo:rustc-link-search=native={}", tundra_dir);
            println!("cargo:rustc-link-lib=pthread");
            println!("cargo:rustc-link-lib=stdc++");
            println!("cargo:rustc-link-lib=GL");
            println!("cargo:rustc-link-lib=X11");
        }

        "macos" => {
            let tundra_dir = format!("{}/t2-output/macos-clang-debug-default", target_dir);
            println!("cargo:rustc-link-search=native={}", tundra_dir);
            println!("cargo:rustc-link-lib=framework=Cocoa");
            println!("cargo:rustc-link-lib=framework=CoreGraphics");
            println!("cargo:rustc-link-lib=framework=IOKit");
            println!("cargo:rustc-link-lib=framework=CoreFoundation");
            println!("cargo:rustc-link-lib=framework=QuartzCore");
            println!("cargo:rustc-link-lib=framework=Metal");
            println!("cargo:rustc-link-lib=framework=MetalKit");
            println!("cargo:rustc-link-lib=c++");
        }

        _ => {}
    }

    println!("cargo:rustc-link-lib=static=glfw");
    println!("cargo:rustc-link-lib=static=ui");
    println!("cargo:rustc-link-lib=static=freetype2");
}

//#[cfg(any(feature = "dynamic", feature = "plugin", feature = "tundra"))]
//fn build_cc(_target_os: &str) {}

#[cfg(not(feature = "tundra"))]
fn build_tundra(_target_os: &str) {}

fn main() {
    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

    println!("cargo:rerun-if-changed=external");
    println!("cargo:rerun-if-changed=c_cpp");
    println!("cargo:rerun-if-changed=tundra.lua");

    //build_cc(target_os);
    build_tundra(target_os);
}
