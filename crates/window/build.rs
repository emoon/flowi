use std::path::Path;

fn add_sources(build: &mut cc::Build, root: &str, files: &[&str]) {
    let root = Path::new(root);
    build.files(files.iter().map(|src| root.join(src)));
}

fn add_includes(build: &mut cc::Build, root: &str, files: &[&str]) {
    build.includes(files.iter().map(|src| format!("{}/{}", root, src)));
}

fn main() {
    let mut build = cc::Build::new();
    let glfw_root = "c_cpp/glfw";

    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

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
}
