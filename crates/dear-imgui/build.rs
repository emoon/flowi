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
        .include("c_cpp/freetype2/include")
        .define("FT2_BUILD_LIBRARY", None);

    add_sources(
        &mut build,
        "c_cpp/freetype2/src",
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
    println!("cargo:rerun-if-changed=c_cpp");

    build_freetype2();

    // Build flowi
    let mut build = cc::Build::new();

    add_includes(
        &mut build,
        "c_cpp",
        &[
            "dear-imgui",
            "freetype2/include",
        ],
    );

    add_sources(
        &mut build,
        "c_cpp",
        &[
            "dear-imgui/imgui.cpp",
            "dear-imgui/imgui_draw.cpp",
            "dear-imgui/imgui_tables.cpp",
            "dear-imgui/imgui_widgets.cpp",
            "dear-imgui/misc/freetype/imgui_freetype.cpp",
        ],
    );

    build.compile("dear-imgui");
}
