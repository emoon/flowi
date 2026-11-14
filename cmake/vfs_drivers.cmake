if(FLOWI_VFS_DRIVER_LIBARCHIVE)
    set(ENABLE_TAR OFF CACHE BOOL "" FORCE)
    set(ENABLE_CPIO OFF CACHE BOOL "" FORCE)
    set(ENABLE_CAT OFF CACHE BOOL "" FORCE)
    set(ENABLE_UNZIP OFF CACHE BOOL "" FORCE)
    set(ENABLE_TEST OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_ACL OFF CACHE BOOL "" FORCE)
    set(ENABLE_XATTR OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # The codec set is pinned so every machine builds the same library with the same
    # DT_NEEDED list, whatever codecs it has installed.
    #
    # On: what the driver's extension list can reach. zlib for zip and tgz, bzip2 for
    # tar.bz2, lzma for tar.xz, tar.lzma and 7z, and the OpenSSL digests the preconfig
    # selects, which reading an encrypted zip or 7z needs.
    set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(ENABLE_BZip2 ON CACHE BOOL "" FORCE)
    set(ENABLE_LZMA ON CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL ON CACHE BOOL "" FORCE)
    # Off: libxml2 and expat serve only XAR, and the remaining codecs serve no extension
    # the driver claims. libxml2 left on also breaks the build on a machine that keeps
    # its headers in /usr/include/libxml2: detection succeeds but the include path is
    # not added.
    set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
    set(ENABLE_WIN32_XMLLITE OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZO OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBB2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCREPOSIX OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCRE2POSIX OFF CACHE BOOL "" FORCE)

    # libarchive's configure step probes about 200 headers and functions. The preconfig
    # files answer those probes up front, which cuts the cmake run from about forty
    # seconds to two and lets the library cross-compile.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(${CMAKE_CURRENT_SOURCE_DIR}/external/libarchive/libarchive_preconfig.cmake)
    elseif(APPLE)
        include(${CMAKE_CURRENT_SOURCE_DIR}/external/libarchive/libarchive_preconfig_macos.cmake)
    elseif(WIN32)
        include(${CMAKE_CURRENT_SOURCE_DIR}/external/libarchive/libarchive_preconfig_windows.cmake)
    endif()

    # The preconfig records what the machine that generated it had, and three of its
    # answers come from optional dev packages: libacl and sys/acl.h, and e2fsprogs'
    # ext2fs/ext2_fs.h. ACL and xattr support is off above and ext2 file flags only
    # matter to a writer, so all three are forced absent and a build needs none of
    # those packages.
    set(HAVE_ACL_LIBACL_H "" CACHE STRING "" FORCE)
    set(HAVE_SYS_ACL_H "" CACHE STRING "" FORCE)
    set(HAVE_EXT2FS_EXT2_FS_H "" CACHE STRING "" FORCE)

    add_subdirectory(external/libarchive)
    if(TARGET archive_static)
        target_compile_options(archive_static PRIVATE -w)
    endif()
endif()

if(FLOWI_VFS_DRIVER_LHASA)
    add_subdirectory(external/lhasa)
endif()

if(FLOWI_VFS_DRIVER_UNLZX)
    add_subdirectory(external/unlzx)
endif()
