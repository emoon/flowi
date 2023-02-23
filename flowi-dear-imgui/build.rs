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
    let root = "cpp/freetype2";
    
    println!("cargo:rerun-if-changed={}", root);

    build
        .cpp(true)
        .warnings(false)
        .include(".")
        .define("FT2_BUILD_LIBRARY", None);


    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

    match target_os {
        "linux" => {
            build.flag("-Wno-enum-compare");
            build.flag("-Wno-unused-but-set-variable");
        }

        "macos" => {
            build.flag("-Wno-enum-compare");
        }

        _ => {}
    }

    add_includes(&mut build, root, &["include"]);
    add_sources(
        &mut build,
        "cpp/freetype2/src",
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

fn build_imgui() {
    let mut build = cc::Build::new();
    let root = "cpp/dear-imgui";
    
    println!("cargo:rerun-if-changed={}", root);

    let os = std::env::var("CARGO_CFG_TARGET_OS").expect("TARGET_OS not specified");
    let target_os = os.as_str();

    match target_os {
        "linux" => {
            build.flag("-Wno-unused-but-set-variable");
        }

        "macos" => {
            build.flag("-Wno-enum-compare");
        }

        _ => {}
    }
    
    add_includes(&mut build, "cpp", &["freetype2/include", "dear-imgui"]);
    add_sources(
        &mut build,
        root,
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


fn main() {
    build_freetype2();
    build_imgui();
}

