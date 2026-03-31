
#ifndef MINI_UNIONFS_H
#define MINI_UNIONFS_H

/*
 * Mini-UnionFS shared header.
 *
 * Goals:
 *   - Build on Linux/WSL (typically libfuse3)
 *   - Build on macOS (typically macFUSE, FUSE 2.x compatible)
 *
 * Note: FUSE2 and FUSE3 differ in some callback signatures (notably getattr
 * and readdir). We keep the codebase portable by:
 *   - including the correct FUSE header when available
 *   - using conditional prototypes based on FUSE_MAJOR_VERSION
 */

/* Select an appropriate API version before including FUSE headers. */
#ifdef __APPLE__
/* macFUSE is FUSE 2.x compatible. */
#  ifndef FUSE_USE_VERSION
#    define FUSE_USE_VERSION 29
#  endif
#else
/* Linux/WSL commonly uses libfuse3. */
#  ifndef FUSE_USE_VERSION
#    define FUSE_USE_VERSION 31
#  endif
#endif

/* Standard headers needed across callbacks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>

/* Choose headers consistent with platform conventions.
 * - macOS: prefer macFUSE (FUSE2 headers)
 * - Linux/WSL: prefer fuse3 headers
 */
#if defined(__has_include)
#  ifdef __APPLE__
#    if __has_include(<fuse/fuse.h>)
#      include <fuse/fuse.h>
#    elif __has_include(<fuse.h>)
#      include <fuse.h>
#    elif __has_include(<fuse3/fuse.h>)
#      include <fuse3/fuse.h>
#    else
#      error "Could not find FUSE headers (macOS). Install macFUSE."
#    endif
#  else
#    if __has_include(<fuse3/fuse.h>)
#      include <fuse3/fuse.h>
#    elif __has_include(<fuse/fuse.h>)
#      include <fuse/fuse.h>
#    elif __has_include(<fuse.h>)
#      include <fuse.h>
#    else
#      error "Could not find FUSE headers. Install libfuse3-dev (Linux/WSL) or macFUSE (macOS)."
#    endif
#  endif
#else
#  include <fuse.h>
#endif

/* Detect FUSE major version at compile time. */
#if defined(FUSE_MAJOR_VERSION) && (FUSE_MAJOR_VERSION >= 3)
#  define MINI_UNIONFS_FUSE3 1
#else
#  define MINI_UNIONFS_FUSE3 0
#endif

struct mini_unionfs_state {
	char *lower_dir;
	char *upper_dir;
};

#define UNIONFS_DATA \
	((struct mini_unionfs_state *) fuse_get_context()->private_data)

#define UNIONFS_PATH_MAX PATH_MAX
#define WHITEOUT_PREFIX ".wh."

int resolve_path(const char *path, char *resolved);

/* Member 1: getattr */
#if MINI_UNIONFS_FUSE3
int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
#else
int unionfs_getattr(const char *path, struct stat *stbuf);
#endif

/* Member 2: readdir/read/open */
#if MINI_UNIONFS_FUSE3
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
					struct fuse_file_info *fi, enum fuse_readdir_flags flags);
#else
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
					struct fuse_file_info *fi);
#endif
int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int unionfs_open(const char *path, struct fuse_file_info *fi);

/* Member 3: write/create/mkdir/rmdir */
int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int unionfs_mkdir(const char *path, mode_t mode);
int unionfs_rmdir(const char *path);

/* Member 4: unlink */
int unionfs_unlink(const char *path);

#endif

