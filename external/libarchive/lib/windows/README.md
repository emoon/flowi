# Prebuilt libarchive for Windows

This directory contains the prebuilt libarchive static library for Windows.

## Files

- `archive_static.lib` - Static library built with clang-cl (Release configuration)

## Version

libarchive 3.7.4 with zlib 1.3.1 support (required for ZIP deflate decompression)

## Building from source

The library is built as part of CI when changes are needed. To rebuild:

1. Set `use_prebuilt_libarchive` to FALSE in `src/plugins/vfs/libarchive/CMakeLists.txt`
2. Build with Release configuration on Windows
3. Copy `build/external/libarchive/libarchive/archive.lib` to this directory as `archive_static.lib`

## Notes

- Built with zlib support for ZIP deflate (compression method 8)
- When linking against this static library, define `LIBARCHIVE_STATIC`
- Also requires linking against `zlibstatic` (bundled zlib)
- Headers are shared from the bundled libarchive source (`external/libarchive/libarchive/`)
