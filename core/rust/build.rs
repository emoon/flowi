fn main() {
    let mut build = cc::Build::new();
    let env = std::env::var("TARGET").unwrap();

    build.define("FT2_BUILD_LIBRARY", None);
    build.include("external/freetype2/build/include");
    build.include("external/freetype2/include");
    build.include("external/freetype2/include/config");

    build.file("external/freetype2/src/autofit/autofit.c");
    build.file("external/freetype2/src/base/ftsystem.c");
    build.file("external/freetype2/src/base/ftbase.c");
    build.file("external/freetype2/src/base/ftbbox.c");
    build.file("external/freetype2/src/base/ftbdf.c");
    build.file("external/freetype2/src/base/ftbitmap.c");
    build.file("external/freetype2/src/base/ftcid.c");
    build.file("external/freetype2/src/base/ftfstype.c");
    build.file("external/freetype2/src/base/ftgasp.c");
    build.file("external/freetype2/src/base/ftglyph.c");
    build.file("external/freetype2/src/base/ftgxval.c");
    build.file("external/freetype2/src/base/ftinit.c");
    build.file("external/freetype2/src/base/ftmm.c");
    build.file("external/freetype2/src/base/ftotval.c");
    build.file("external/freetype2/src/base/ftpatent.c");
    build.file("external/freetype2/src/base/ftpfr.c");
    build.file("external/freetype2/src/base/ftstroke.c");
    build.file("external/freetype2/src/base/ftsynth.c");
    build.file("external/freetype2/src/base/fttype1.c");
    build.file("external/freetype2/src/base/ftwinfnt.c");
    build.file("external/freetype2/src/bdf/bdf.c");
    build.file("external/freetype2/src/bzip2/ftbzip2.c");
    build.file("external/freetype2/src/cache/ftcache.c");
    build.file("external/freetype2/src/cff/cff.c");
    build.file("external/freetype2/src/cid/type1cid.c");
    build.file("external/freetype2/src/gzip/ftgzip.c");
    build.file("external/freetype2/src/lzw/ftlzw.c");
    build.file("external/freetype2/src/pcf/pcf.c");
    build.file("external/freetype2/src/pfr/pfr.c");
    build.file("external/freetype2/src/psaux/psaux.c");
    build.file("external/freetype2/src/pshinter/pshinter.c");
    build.file("external/freetype2/src/psnames/psnames.c");
    build.file("external/freetype2/src/raster/raster.c");
    build.file("external/freetype2/src/sdf/sdf.c");
    build.file("external/freetype2/src/sfnt/sfnt.c");
    build.file("external/freetype2/src/smooth/smooth.c");
    build.file("external/freetype2/src/truetype/truetype.c");
    build.file("external/freetype2/src/type1/type1.c");
    build.file("external/freetype2/src/type42/type42.c");
    build.file("external/freetype2/src/winfonts/winfnt.c");

    if env.contains("windows") {
        build.file("external/freetype2/src/builds/windows/ftdebug.c");
    } else {
        build.file("external/freetype2/src/base/ftdebug.c");
    }

    build.compile("freetype2");

    /*
    // build harfbuzz
    build = cc::Build::new();
    build.cpp(true)
        .flag("-std=c++11")
        .warnings(false)
        .file("external/harfbuzz/src/harfbuzz.cc");

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

    build.include("external/freetype2/build/include");
    build.file("src/c/font_cache.c");
    build.compile("flowi_c");
}

