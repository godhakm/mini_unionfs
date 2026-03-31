#include "mini_unionfs.h"

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags) { return -ENOSYS; }

int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi) { return -ENOSYS; }

int unionfs_open(const char *path, struct fuse_file_info *fi) { return -ENOSYS; }
