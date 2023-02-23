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
    let root = "../../";

    println!("cargo:rerun-if-changed=cpp");

    build.define("BX_CONFIG_DEBUG", "0");

    add_includes(&mut build, "../", &["langs/c/include"]);
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

    add_includes(&mut build, root, 
        &[
            "include", 
            "src", 
            "flowi-dear-imgui/cpp", 
            "flowi-dear-imgui/cpp/dear-imgui",
            "flowi-dear-imgui/cpp/freetype2/include"
        ],
    );

    add_sources(
        &mut build,
        "",
        &[
            "cpp/application.cpp",
            "cpp/area.c",
            "cpp/array.c",
            "cpp/atlas.c",
            "cpp/command_buffer.c",
            "cpp/font.cpp",
            "cpp/flowi.cpp",
            "cpp/glfw_input.cpp",
            "cpp/handles.c",
            "cpp/image.c",
            "cpp/imgui_wrap.cpp",
            "cpp/io.c",
            "cpp/layer.c",
            "cpp/linear_allocator.c",
            "cpp/primitive_rect.c",
            "cpp/string_allocator.c",
            "cpp/style.cpp",
            "cpp/text.c",
            "cpp/vertex_allocator.c",
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

    build.compile("flowi-ui");
}
