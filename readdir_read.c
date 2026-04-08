#include "mini_unionfs.h"

static void build_layer_path(char *dest, size_t len, const char *layer,
                             const char *fuse_path)
{
    if (strcmp(fuse_path, "/") == 0) {
        snprintf(dest, len, "%s", layer);
    } else {
        snprintf(dest, len, "%s%s", layer, fuse_path);
    }
}

static bool has_seen(const char seen[][NAME_MAX + 1], size_t seen_count,
                     const char *name)
{
    for (size_t i = 0; i < seen_count; i++) {
        if (strncmp(seen[i], name, NAME_MAX) == 0) {
            return true;
        }
    }
    return false;
}

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;

    struct mini_unionfs_state *state = UNIONFS_DATA;
    char seen[256][NAME_MAX + 1];
    size_t seen_count = 0;

    if (filler(buf, ".", NULL, 0, 0) != 0) {
        return -ENOMEM;
    }
    if (filler(buf, "..", NULL, 0, 0) != 0) {
        return -ENOMEM;
    }

    char dir_path[UNIONFS_PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    build_layer_path(dir_path, sizeof(dir_path), state->upper_dir, path);
    dir = opendir(dir_path);
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            const char *name = entry->d_name;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }
            if (filler(buf, name, NULL, 0, 0) != 0) {
                closedir(dir);
                return -ENOMEM;
            }
            if (seen_count < sizeof(seen) / sizeof(seen[0])) {
                strncpy(seen[seen_count], name, NAME_MAX);
                seen[seen_count][NAME_MAX] = '\0';
                seen_count++;
            }
        }
        closedir(dir);
    } else if (errno != ENOENT) {
        return -errno;
    }

    build_layer_path(dir_path, sizeof(dir_path), state->lower_dir, path);
    dir = opendir(dir_path);
    if (dir == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        return -errno;
    }

    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (has_seen(seen, seen_count, name)) {
            continue;
        }

        char whiteout_path[UNIONFS_PATH_MAX];
        if (strcmp(path, "/") == 0) {
            snprintf(whiteout_path, sizeof(whiteout_path), "%s/%s%s",
                     state->upper_dir, WHITEOUT_PREFIX, name);
        } else {
            snprintf(whiteout_path, sizeof(whiteout_path), "%s%s/%s%s",
                     state->upper_dir, path, WHITEOUT_PREFIX, name);
        }
        if (access(whiteout_path, F_OK) == 0) {
            continue;
        }

        if (filler(buf, name, NULL, 0, 0) != 0) {
            closedir(dir);
            return -ENOMEM;
        }
    }

    closedir(dir);
    return 0;
}

int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi)
{
    (void) fi;

    char resolved[UNIONFS_PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0) {
        return res;
    }

    int fd = open(resolved, O_RDONLY);
    if (fd == -1) {
        return -errno;
    }

    ssize_t bytes = pread(fd, buf, size, offset);
    if (bytes == -1) {
        bytes = -errno;
    }
    close(fd);
    return (int) bytes;
}

int unionfs_open(const char *path, struct fuse_file_info *fi)
{
    (void) fi;

    char resolved[UNIONFS_PATH_MAX];
    return resolve_path(path, resolved);
}
