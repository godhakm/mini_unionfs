#include "mini_unionfs.h"

int unionfs_unlink(const char *path)
{
    if (strcmp(path, "/") == 0) {
        return -EISDIR;
    }

    struct mini_unionfs_state *state = UNIONFS_DATA;
    char upper_path[UNIONFS_PATH_MAX];
    snprintf(upper_path, sizeof(upper_path), "%s%s", state->upper_dir, path);

    if (access(upper_path, F_OK) == 0) {
        if (unlink(upper_path) == -1) {
            return -errno;
        }
        return 0;
    }

    char resolved[UNIONFS_PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0) {
        return res;
    }

    return unionfs_create_whiteout(path);
}
