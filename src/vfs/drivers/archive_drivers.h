#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Archive driver descriptors
//
// Each archive driver is compiled in only when its FLOWI_VFS_DRIVER_* cmake option is on, and its accessor is
// declared only then, so a call to one that was left out is a compile error rather than a link error.
//
// Each accessor returns a pointer to the driver's own static descriptor, not a copy: writing through it changes
// what every later registration sees.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/vfs/vfs_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#if FLOWI_VFS_DRIVER_LIBARCHIVE
FlVfsPlugin* vfs_driver_libarchive(void);
#endif
#if FLOWI_VFS_DRIVER_LHASA
FlVfsPlugin* vfs_driver_lhasa(void);
#endif
#if FLOWI_VFS_DRIVER_UNLZX
FlVfsPlugin* vfs_driver_unlzx(void);
#endif

#ifdef __cplusplus
}
#endif
