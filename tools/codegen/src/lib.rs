//! flowi's api_gen configuration, shared by the regenerate binary and the drift test.
//!
//! flowi has three independent codegen targets: the foundation (`api/core`), the VFS (`api/vfs`) and
//! the toolkit API. All are listed here so `cargo regen` produces all of flowi's committed codegen and `cargo regen-check`
//! guards all of it.
//!
//! The foundation and the VFS emit their headers and bindings but no export list: one libflowi
//! exports all three layers, so the toolkit target writes the single list that covers them.
//! `reference_dirs` lets a def resolve the layout of a type it embeds by value from a layer below
//! while emitting nothing for it.

use std::path::{Path, PathBuf};

use api_gen::naming::Naming;
use api_gen::Config;

/// One codegen target: what it is called, which directories it owns, and how to configure it.
pub struct Target {
    /// Human name, used in drift reports.
    pub name: &'static str,
    /// Repo-relative directories this target writes into.
    pub outputs: &'static [&'static str],
    /// The config, with outputs rooted at `out_root`.
    pub config: Config,
}

/// Every codegen target, with outputs rooted at `out_root`. The IDL always comes from
/// `repo`; the two roots are the same tree for a real regeneration, and differ only for the
/// drift test, which writes to a temp dir and compares.
pub fn targets(repo: &Path, out_root: &Path) -> Vec<Target> {
    vec![
        Target {
            name: "core API",
            outputs: &[
                "include/flowi/arena",
                "include/flowi/core",
                "include/flowi/heap",
                "include/flowi/path",
                "include/flowi/string",
                "rust/flowi/src/sys/foundation",
            ],
            config: Config {
                naming: Naming::new("Fl", "fl"),
                api_dir: repo.join("api/core"),
                c_include_root: Some(out_root.join("include/flowi")),
                c_include_prefix: Some("flowi".to_owned()),
                c_extern_c: true,
                rust_dir: Some(out_root.join("rust/flowi/src/sys/foundation")),
                rust_root: Some("crate::sys".to_owned()),
                rust_allow_dead_code: true,
                gen_script: Some("cargo regen".to_owned()),
                ..Default::default()
            },
        },
        Target {
            name: "VFS API",
            outputs: &[
                "include/flowi/vfs",
                "rust/flowi/src/sys/vfs_abi",
            ],
            config: Config {
                naming: Naming::new("Fl", "fl"),
                api_dir: repo.join("api/vfs"),
                reference_dirs: vec![repo.join("api/core")],
                c_include_root: Some(out_root.join("include/flowi")),
                c_include_prefix: Some("flowi".to_owned()),
                c_extern_c: true,
                rust_dir: Some(out_root.join("rust/flowi/src/sys/vfs_abi")),
                rust_root: Some("crate::sys".to_owned()),
                rust_allow_dead_code: true,
                gen_script: Some("cargo regen".to_owned()),
                ..Default::default()
            },
        },
        Target {
            name: "toolkit API",
            outputs: &[
                "include/flowi/app",
                "include/flowi/platform",
                "include/flowi/ui",
                "rust/flowi/src/sys/toolkit",
                "rust/flowi/src/generated",
                "src/exports",
            ],
            config: Config {
                naming: Naming::new("Fl", "fl"),
                api_dir: repo.join("api"),
                reference_dirs: vec![repo.join("api/core"), repo.join("api/vfs")],
                c_include_root: Some(out_root.join("include/flowi")),
                c_include_prefix: Some("flowi".to_owned()),
                c_extern_c: true,
                rust_dir: Some(out_root.join("rust/flowi/src/sys/toolkit")),
                facade_dir: Some(out_root.join("rust/flowi/src/generated")),
                facade_raw_path: Some("crate::sys".to_owned()),
                facade_frame_guard: Some("crate::frame_thread::frame_thread_guard".to_owned()),
                export_dir: Some(out_root.join("src/exports")),
                // One libflowi exports the foundation and the VFS beside the toolkit, so
                // their defs' symbols and the hand-written headers' share one list.
                export_include_references: true,
                export_extra_symbol_files: vec![
                    repo.join("tools/codegen/exports/handwritten.txt"),
                    repo.join("tools/codegen/exports/rust_facade.txt"),
                ],
                rust_root: Some("crate::sys".to_owned()),
                rust_allow_dead_code: true,
                gen_script: Some("cargo regen".to_owned()),
                ..Default::default()
            },
        },
    ]
}

/// The repository root, one level above this crate.
pub fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(2)
        .expect("tools/codegen/ sits two levels below the repo root")
        .to_path_buf()
}
