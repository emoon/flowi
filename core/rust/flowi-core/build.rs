use cc;
use std::path::Path;

fn main() {
    let mut build = cc::Build::new();
    let env = std::env::var("TARGET").unwrap();

    let extern_in_tree_dir = "../../c/external";
    let extern_internal_dir = "external";
    let flowi_in_tree_dir = "../../c";
    let flowi_internal_dir = "flowi_c";

    // First we try to build from the in the tree, if that doesn't exists we build from
    // with in the crate instead

    let external_dir = Path::new(if Path::new(extern_in_tree_dir).exists() {
        extern_in_tree_dir
    } else {
        extern_internal_dir
    });

    let flowi_dir = Path::new(if Path::new(flowi_in_tree_dir).exists() {
        flowi_in_tree_dir
    } else {
        flowi_internal_dir
    });

    let freetype_files = [
        "freetype2/src/autofit/autofit.c",
        "freetype2/src/base/ftsystem.c",
        "freetype2/src/base/ftbase.c",
        "freetype2/src/base/ftbbox.c",
        "freetype2/src/base/ftbdf.c",
        "freetype2/src/base/ftbitmap.c",
        "freetype2/src/base/ftcid.c",
        "freetype2/src/base/ftfstype.c",
        "freetype2/src/base/ftgasp.c",
        "freetype2/src/base/ftglyph.c",
        "freetype2/src/base/ftgxval.c",
        "freetype2/src/base/ftinit.c",
        "freetype2/src/base/ftmm.c",
        "freetype2/src/base/ftotval.c",
        "freetype2/src/base/ftpatent.c",
        "freetype2/src/base/ftstroke.c",
        "freetype2/src/base/ftpfr.c",
        "freetype2/src/base/ftsynth.c",
        "freetype2/src/base/fttype1.c",
        "freetype2/src/base/ftwinfnt.c",
        "freetype2/src/bdf/bdf.c",
        "freetype2/src/bzip2/ftbzip2.c",
        "freetype2/src/cache/ftcache.c",
        "freetype2/src/cff/cff.c",
        "freetype2/src/cid/type1cid.c",
        "freetype2/src/gzip/ftgzip.c",
        "freetype2/src/lzw/ftlzw.c",
        "freetype2/src/pcf/pcf.c",
        "freetype2/src/pfr/pfr.c",
        "freetype2/src/psaux/psaux.c",
        "freetype2/src/pshinter/pshinter.c",
        "freetype2/src/psnames/psnames.c",
        "freetype2/src/raster/raster.c",
        "freetype2/src/sdf/sdf.c",
        "freetype2/src/sfnt/sfnt.c",
        "freetype2/src/smooth/smooth.c",
        "freetype2/src/truetype/truetype.c",
        "freetype2/src/type1/type1.c",
        "freetype2/src/type42/type42.c",
        "freetype2/src/winfonts/winfnt.c",
    ];

    let freetype_include_dirs = [
        "freetype2/build/include",
        "freetype2/include",
        "freetype2/include/config",
    ];

    for include in freetype_include_dirs {
        build.include(external_dir.join(include));
    }

    build.define("FT2_BUILD_LIBRARY", None);

    for file in freetype_files {
        build.file(external_dir.join(file));
    }

    if env.contains("windows") {
        build.file(external_dir.join("freetype2/src/builds/windows/ftdebug.c"));
    } else {
        build.file(external_dir.join("freetype2/src/base/ftdebug.c"));
    }

    build.compile("freetype2");

    // Build stb
    cc::Build::new()
        .file(external_dir.join("stb/stb.c"))
        .compile("stb");

    // Build flowi
    build = cc::Build::new();

    let flowi_files = [
        "src/area.c",
        "src/flowi.c",
        "src/image.c",
        "src/style.c",
        "src/array.c",
        "src/font.c",
        "src/io.c",
        "src/text.c",
        "src/atlas.c",
        "src/font_cache.c",
        "src/layout.c",
        "src/vertex_allocator.c",
        "src/command_buffer.c",
        "src/handles.c",
        "src/linear_allocator.c",
    ];

    for file in flowi_files {
        build.file(flowi_dir.join(file));
    }

    build.include(flowi_dir.join("include"));
    build.include(external_dir.join("freetype2/include"));
    build.include(external_dir.join("tlslf"));
    build.include(external_dir.join("stb"));

    build.compile("flowi");

    /*
    // build harfbuzz
    build = cc::Build::new();
    build.cpp(true)
        .flag("-std=c++11")
        .warnings(false)
        .file("harfbuzz/src/harfbuzz.cc");

    if !env.contains("windows") {
        build.define("HAVE_PTHREAD", "1");
    }

    if env.contains("apple") {
        build.define("HAVE_CORETEXT", "1");
    }

    if env.contains("windows-gnu") {
        build.flag("-Wa,-mbig-obj");
    }

    build.compile("embedded_harfbuzz");

    // build the c code for the lib
    build = cc::Build::new();
    */

    //build.include("freetype2/build/include");
    //build.file("src/c/font_cache.c");
    //build.compile("flowi_c");
}
