// Links libflowi into this crate. Resolution order, first match wins:
//   1. `FLOWI_SKIP_NATIVE_LINK=1`  link nothing.
//   2. `FLOWI_LIB_DIR`             link a prebuilt shared library from that directory.
//   3. otherwise                   build the `flowi` library with cmake and link it, as a
//                                  shared library or, with the `static` feature, an archive.

use std::path::{Path, PathBuf};

fn main() {
    link_flstring_probe();
    link_native();
}

fn link_native() {
    println!("cargo:rerun-if-env-changed=FLOWI_SKIP_NATIVE_LINK");
    if std::env::var_os("FLOWI_SKIP_NATIVE_LINK").is_some_and(|v| v == "1") {
        return;
    }

    println!("cargo:rerun-if-env-changed=FLOWI_LIB_DIR");
    if let Some(lib_dir) = std::env::var_os("FLOWI_LIB_DIR").filter(|v| !v.is_empty()) {
        let lib_dir = Path::new(&lib_dir);
        assert!(
            lib_dir.is_dir(),
            "FLOWI_LIB_DIR does not name a directory: {} — build the flowi shared \
             library there first (or unset the variable to let this script build it)",
            lib_dir.display()
        );
        println!("cargo:rerun-if-changed={}", lib_dir.display());
        emit_shared_link(lib_dir);
        return;
    }

    let repo_root = repo_root();
    let windowed = if cfg!(feature = "windowed") {
        "ON"
    } else {
        "OFF"
    };
    let static_link = cfg!(feature = "static");
    let dst = cmake::Config::new(&repo_root)
        .define("FLOWI_SDL_BACKEND", windowed)
        .define("FT_DISABLE_PNG", "ON")
        .define("FT_DISABLE_BZIP2", "ON")
        .define("FT_DISABLE_HARFBUZZ", "ON")
        .define("FT_DISABLE_BROTLI", "ON")
        .build_target(if static_link { "flowi_static" } else { "flowi" })
        .build();

    for input in ["src", "include", "api", "cmake", "external", "CMakeLists.txt"] {
        let path = repo_root.join(input);
        assert!(
            path.exists(),
            "native build input does not exist: {}",
            path.display()
        );
        println!("cargo:rerun-if-changed={}", path.display());
    }

    if static_link {
        emit_static_link(&dst.join("build"));
    } else {
        emit_shared_link(&dst.join("build"));
    }
}

fn repo_root() -> PathBuf {
    let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
    manifest_dir
        .parent() // rust/
        .and_then(Path::parent) // repo root
        .unwrap_or_else(|| {
            panic!(
                "{} must live at <repo>/rust/<crate>",
                manifest_dir.display()
            )
        })
        .to_path_buf()
}

// Links `libflowi` from `dir`, with an rpath to it.
fn emit_shared_link(dir: &Path) {
    println!("cargo:rustc-link-search=native={}", dir.display());
    println!("cargo:rustc-link-lib=dylib=flowi");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
}

// Links `libflowi_static.a`, then every other archive the cmake build produced (freetype, mimalloc, ...),
// then the system libraries the native build needs.
fn emit_static_link(build_dir: &Path) {
    let mut archives = Vec::new();
    collect_archives(build_dir, &mut archives);
    archives.sort();
    for dir in archives.iter().filter_map(|a| a.parent()).collect::<std::collections::BTreeSet<_>>() {
        println!("cargo:rustc-link-search=native={}", dir.display());
    }
    println!("cargo:rustc-link-lib=static=flowi_static");
    for archive in &archives {
        let name = archive.file_stem().and_then(|n| n.to_str()).unwrap_or_default();
        let name = name.strip_prefix("lib").unwrap_or(name);
        if name != "flowi_static" {
            println!("cargo:rustc-link-lib=static={name}");
        }
    }

    let windowed = cfg!(feature = "windowed");
    if cfg!(target_os = "macos") {
        for framework in ["CoreServices", "CoreFoundation", "Metal", "Foundation", "IOSurface"] {
            println!("cargo:rustc-link-lib=framework={framework}");
        }
        if windowed {
            println!("cargo:rustc-link-lib=framework=QuartzCore");
        }
        println!("cargo:rustc-link-lib=dylib=z");
    } else if cfg!(target_os = "linux") {
        for lib in ["m", "z", "EGL", "OpenGL"] {
            println!("cargo:rustc-link-lib=dylib={lib}");
        }
    }
    if windowed {
        println!("cargo:rustc-link-lib=dylib=SDL2");
    }
}

fn collect_archives(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect_archives(&path, out);
        } else if path.extension().is_some_and(|e| e == "a" || e == "lib") {
            out.push(path);
        }
    }
}

// Compiles the C FlString ABI probe and links it into every linked binary (the lib unit tests included).
fn link_flstring_probe() {
    let root = repo_root();
    let probe = root.join("tests/probes/flstring_abi_probe.c");
    println!("cargo:rerun-if-changed={}", probe.display());
    if !probe.exists() {
        return;
    }
    println!(
        "cargo:rerun-if-changed={}",
        root.join("tests/probes/external_abi_probe.h").display()
    );
    let objects = cc::Build::new()
        .cargo_metadata(false)
        .include(root.join("include"))
        .include(root.join("tests/probes"))
        .file(&probe)
        .try_compile_intermediates()
        .unwrap_or_else(|e| panic!("compiling flstring_abi_probe.c failed: {e}"));
    for object in objects {
        println!("cargo:rustc-link-arg={}", object.display());
    }
}
