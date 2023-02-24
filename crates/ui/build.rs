use std::path::Path;

fn add_sources(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = Path::new(root);
    build.files(files.iter().map(|src| root.join(src)));
}

fn add_includes(build: &mut cc::Build, root: &str, files: &[&str]) {
    build.includes(files.iter().map(|src| format!("{}/{}", root, src)));
}

fn main() {
    // Build flowi
    let mut build = cc::Build::new();

    println!("cargo:rerun-if-changed=c_cpp");

    build.define("BX_CONFIG_DEBUG", "0");

    add_includes(&mut build, "..", 
        &[
            "../langs/c_cpp/include",
            "window/c_cpp/glfw/include",
            "render/c_cpp/bgfx/include",
            "render/c_cpp/bx/include",
            "dear-imgui/c_cpp", 
            "dear-imgui/c_cpp/dear-imgui",
            "dear-imgui/c_cpp/freetype2/include"
        ],
    );

    add_sources(
        &mut build,
        "c_cpp",
        &[
            "application.cpp",
            "area.c",
            "array.c",
            "atlas.c",
            "command_buffer.c",
            "font.cpp",
            "flowi.cpp",
            "glfw_input.cpp",
            "handles.c",
            "imgui_wrap.cpp",
            "io.c",
            "layer.c",
            "linear_allocator.c",
            "primitive_rect.c",
            "string_allocator.c",
            "style.cpp",
            "text.c",
            "vertex_allocator.c",
        ],
    );

    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

    match target_os {
        "linux" => {
            build.define("GLFW_EXPOSE_NATIVE_X11", None);
        }

        "windows" => {
            add_includes(
                &mut build,
                "..",
                &["render/c_cpp/bx/include/compat/msvc", "render/c_cpp/bgfx/3rdparty/dxsdk/include"],
            );

            build.define("GLFW_EXPOSE_NATIVE_WIN32", None);
        }

        "macos" => {
            build.define("GLFW_EXPOSE_NATIVE_COCOA", None);
        }

        unsupported => unimplemented!("{} is not a supported target", unsupported),
    }

    build.compile("window");
}
