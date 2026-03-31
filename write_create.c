#include "mini_unionfs.h"

int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi) { return -ENOSYS; }

int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi) { return -ENOSYS; }

int unionfs_mkdir(const char *path, mode_t mode) { return -ENOSYS; }

int unionfs_rmdir(const char *path) { return -ENOSYS; }
