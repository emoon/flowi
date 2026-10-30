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

    # Pin the codec set rather than taking whatever happens to be installed, so a clone
    # builds the same library everywhere and the shared build's DT_NEEDED list is a fact
    # rather than a property of the build machine.
    #
    # On is everything the driver's own extension list can reach: zlib for zip and tgz,
    # bzip2 for tar.bz2, lzma for tar.xz / tar.lzma / 7z, and the OpenSSL digests the
    # preconfig already selected, which is what reads an encrypted zip or 7z.
    set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(ENABLE_BZip2 ON CACHE BOOL "" FORCE)
    set(ENABLE_LZMA ON CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL ON CACHE BOOL "" FORCE)
    # Off is everything it cannot: libxml2 and expat serve XAR alone, and the remaining
    # codecs serve no extension the driver claims. Leaving libxml2 on is also what breaks
    # the build outright on a machine that has the library but puts its headers in
    # /usr/include/libxml2 - detection succeeds and the include path does not follow.
    set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
    set(ENABLE_WIN32_XMLLITE OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZO OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBB2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCREPOSIX OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCRE2POSIX OFF CACHE BOOL "" FORCE)

    # libarchive's own configure step probes ~200 headers and functions. The preconfig
    # files answer those probes up front, which is the difference between a two-second
    # and a forty-second cmake run, and is what makes cross-compilation possible at all.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(${CMAKE_CURRENT_SOURCE_DIR}/external/libarchive/libarchive_preconfig.cmake)
    elseif(APPLE)
        include(${CMAKE_CURRENT_SOURCE_DIR}/external/libarchive/libarchive_preconfig_macos.cmake)
    elseif(WIN32)
        include(${CMAKE_CURRENT_SOURCE_DIR}/external/libarchive/libarchive_preconfig_windows.cmake)
    endif()

    # The preconfig answers its probes with whatever the machine that generated it had, and
    # three of those answers are optional dev packages rather than facts: libacl and
    # sys/acl.h, and e2fsprogs' ext2fs/ext2_fs.h. ACL and xattr support is off above, and
    # ext2 file flags are a writer's concern, so force all three absent rather than make a
    # clone install packages to build a reader.
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
