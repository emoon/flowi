#include <flowi/vfs/drivers.h>
#include <vfs/drivers/archive_drivers.h>

#include <vfs/vfs.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bulk registration of the archive drivers this build contains.
//
// fl_vfs_register_driver splices each driver in behind the previous one and ahead of the LocalFS fallback, so the
// first registered is tried first. lhasa and unlzx register before libarchive and are asked before it.

void fl_vfs_register_builtin_drivers(void) {
#if FLOWI_VFS_DRIVER_LHASA
    fl_vfs_register_driver(vfs_driver_lhasa(), nullptr);
#endif
#if FLOWI_VFS_DRIVER_UNLZX
    fl_vfs_register_driver(vfs_driver_unlzx(), nullptr);
#endif
#if FLOWI_VFS_DRIVER_LIBARCHIVE
    fl_vfs_register_driver(vfs_driver_libarchive(), nullptr);
#endif
}
