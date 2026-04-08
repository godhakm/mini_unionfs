#ifndef MINI_UNIONFS_H
#define MINI_UNIONFS_H

#define FUSE_USE_VERSION 31

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define UNIONFS_PATH_MAX 4096
#define WHITEOUT_PREFIX ".wh."

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

int resolve_path(const char *path, char *resolved);
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi);
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags);
int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi);
int unionfs_open(const char *path, struct fuse_file_info *fi);
int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi);
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi);
int unionfs_mkdir(const char *path, mode_t mode);
int unionfs_rmdir(const char *path);
int unionfs_unlink(const char *path);
int unionfs_create_whiteout(const char *path);

#endif /* MINI_UNIONFS_H */
