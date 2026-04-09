#include "mini_unionfs.h"

/* --------------------------------------------------------------------------
 * write_create.c overview
 *
 * This module owns every mutating operation:
 *   • ensure_upper_dir_exists()/ensure_upper_parent_dir() lazily build the
 *     directory structure in the upper layer so new files have somewhere to go.
 *   • cow_copy() performs Copy-on-Write when a user edits a file that only
 *     lives in lower/, duplicating it into upper/ before any write.
 *   • unionfs_write/create/mkdir/rmdir() funnel all writes into upper/ and
 *     create whiteouts when removing lower-only entries.
 *   • unionfs_create_whiteout() drops `.wh.<name>` markers so resolve_path()
 *     hides the original lower entry.
 *
 * Example scenario:
 *   1. lower/base.txt exists; upper/base.txt does not.
 *   2. User runs `echo "hi" >> mnt/base.txt`. unionfs_write resolves the
 *      lower path, calls cow_copy("/base.txt"), copies it into upper/, and
 *      appends there.
 *   3. User deletes `mnt/base.txt`. unionfs_rmdir calls unionfs_create_whiteout,
 *      producing upper/.wh.base.txt so the lower file stays hidden but intact.
 */

/* ensure_upper_dir_exists()
 * Example: virtual_dir="shared/subdir" with upper_dir=/layers/upper
 * creates /layers/upper/shared then /layers/upper/shared/subdir so later
 * writes to mnt/shared/subdir/... have a valid parent chain.
 */
static int ensure_upper_dir_exists(const char *virtual_dir)
{
    if (virtual_dir == NULL || virtual_dir[0] == '\0') {
        return 0;
    }

    struct mini_unionfs_state *state = UNIONFS_DATA;
    char partial[UNIONFS_PATH_MAX];
    snprintf(partial, sizeof(partial), "%s", state->upper_dir);

    char dir_copy[UNIONFS_PATH_MAX];
    strncpy(dir_copy, virtual_dir, sizeof(dir_copy));
    dir_copy[sizeof(dir_copy) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(dir_copy, "/", &saveptr);
    while (token != NULL) {
        strncat(partial, "/", sizeof(partial) - strlen(partial) - 1);
        strncat(partial, token, sizeof(partial) - strlen(partial) - 1);
        if (mkdir(partial, 0755) == -1 && errno != EEXIST) {
            return -errno;
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    return 0;
}

/* ensure_upper_parent_dir()
 * Example: path="/docs/report.txt" -> ensures "/docs" exists inside upper/.
 * Root-level files like "/foo.txt" have no parent, so we simply return 0.
 */
static int ensure_upper_parent_dir(const char *path)
{
    char tmp[UNIONFS_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp) - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (slash == NULL || slash == tmp) {
        return 0;
    }

    *slash = '\0';
    return ensure_upper_dir_exists(tmp);
}

/* build_upper_path()
 * Example: upper_dir=/layers/upper, path=/docs/report.txt -> /layers/upper/docs/report.txt.
 * When path is just "/", we return upper_dir itself.
 */
static void build_upper_path(const char *path, char *dst, size_t len)
{
    struct mini_unionfs_state *state = UNIONFS_DATA;
    if (strcmp(path, "/") == 0) {
        snprintf(dst, len, "%s", state->upper_dir);
    } else {
        snprintf(dst, len, "%s%s", state->upper_dir, path);
    }
}

/* cow_copy()
 * Example: user edits mnt/base.txt but only lower/base.txt exists. We copy
 * lower/base.txt -> upper/base.txt byte-for-byte so future writes go to upper/.
 */
static int cow_copy(const char *path)
{
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char lower_path[UNIONFS_PATH_MAX];
    char upper_path[UNIONFS_PATH_MAX];

    snprintf(lower_path, sizeof(lower_path), "%s%s", state->lower_dir, path);
    build_upper_path(path, upper_path, sizeof(upper_path));

    int res = ensure_upper_parent_dir(path);
    if (res != 0) {
        return res;
    }

    int src = open(lower_path, O_RDONLY);
    if (src == -1) {
        return -errno;
    }

    int dst = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dst == -1) {
        int err = -errno;
        close(src);
        return err;
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(src, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst, buf + written, (size_t)(n - written));
            if (w == -1) {
                int err = -errno;
                close(src);
                close(dst);
                return err;
            }
            written += w;
        }
    }
    if (n == -1) {
        int err = -errno;
        close(src);
        close(dst);
        return err;
    }

    close(src);
    close(dst);
    return 0;
}

/* unionfs_write()
 * Example: echo "hi" >> mnt/base.txt -> resolve_path() hits lower/base.txt, so
 * cow_copy() runs, then we pwrite() "hi" into upper/base.txt at the tail offset.
 */

int unionfs_write(const char *path, const char *buf, size_t size,
                  off_t offset, struct fuse_file_info *fi)
{
    (void) fi;

    struct mini_unionfs_state *state = UNIONFS_DATA;
    char resolved[UNIONFS_PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0) {
        return res;
    }

    char upper_path[UNIONFS_PATH_MAX];
    build_upper_path(path, upper_path, sizeof(upper_path));

    size_t lower_len = strlen(state->lower_dir);
    if (strncmp(resolved, state->lower_dir, lower_len) == 0) {
        res = cow_copy(path);
        if (res != 0) {
            return res;
        }
    }

    int fd = open(upper_path, O_WRONLY);
    if (fd == -1) {
        return -errno;
    }

    ssize_t bytes = pwrite(fd, buf, size, offset);
    if (bytes == -1) {
        bytes = -errno;
    }
    close(fd);
    return (int) bytes;
}

/* unionfs_create()
 * Example: touch mnt/custom.txt -> ensure_upper_parent_dir("/custom.txt")
 * (no-op) then open() creates upper/custom.txt with the requested mode.
 */
int unionfs_create(const char *path, mode_t mode,
                   struct fuse_file_info *fi)
{
    (void) fi;

    int res = ensure_upper_parent_dir(path);
    if (res != 0) {
        return res;
    }

    char upper_path[UNIONFS_PATH_MAX];
    build_upper_path(path, upper_path, sizeof(upper_path));

    int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd == -1) {
        return -errno;
    }
    close(fd);
    return 0;
}

/* unionfs_mkdir()
 * Example: mkdir -p mnt/projects/demo -> we ensure "/projects" exists in upper/
 * and create "/projects/demo" there, leaving lower/ untouched.
 */
int unionfs_mkdir(const char *path, mode_t mode)
{
    int res = ensure_upper_parent_dir(path);
    if (res != 0) {
        return res;
    }

    char upper_path[UNIONFS_PATH_MAX];
    build_upper_path(path, upper_path, sizeof(upper_path));
    if (mkdir(upper_path, mode) == -1) {
        return -errno;
    }
    return 0;
}


/* unionfs_create_whiteout()
 * Example: deleting mnt/delete_me.txt that only lives in lower/. We create
 * upper/.wh.delete_me.txt so resolve_path() treats the lower file as removed.
 */

int unionfs_create_whiteout(const char *path)
{
    struct mini_unionfs_state *state = UNIONFS_DATA;
    char temp[UNIONFS_PATH_MAX];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp) - 1] = '\0';

    char *base = strrchr(temp, '/');
    const char *name = temp;
    if (base != NULL) {
        name = base + 1;
    }

    char dir_part[UNIONFS_PATH_MAX] = "";
    if (base != NULL) {
        if (base == temp) {
            dir_part[0] = '\0';
        } else {
            *base = '\0';
            snprintf(dir_part, sizeof(dir_part), "%s", temp);
        }
    }

    if (dir_part[0] != '\0') {
        int res = ensure_upper_dir_exists(dir_part);
        if (res != 0) {
            return res;
        }
    }

    char whiteout_path[UNIONFS_PATH_MAX];
    if (dir_part[0] == '\0') {
        snprintf(whiteout_path, sizeof(whiteout_path), "%s/%s%s",
                 state->upper_dir, WHITEOUT_PREFIX, name);
    } else {
        snprintf(whiteout_path, sizeof(whiteout_path), "%s/%s/%s%s",
                 state->upper_dir, dir_part, WHITEOUT_PREFIX, name);
    }

    int fd = open(whiteout_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) {
        return -errno;
    }
    close(fd);
    return 0;
}

/* unionfs_rmdir()
 * Example A: mnt/mydir exists in upper/ -> rmdir upper/mydir.
 * Example B: mnt/shared exists only in lower/ -> create upper/.wh.shared to hide it.
 */

int unionfs_rmdir(const char *path)
{
    char upper_path[UNIONFS_PATH_MAX];
    build_upper_path(path, upper_path, sizeof(upper_path));

    if (access(upper_path, F_OK) == 0) {
        if (rmdir(upper_path) == -1) {
            return -errno;
        }
        return 0;
    }

    return unionfs_create_whiteout(path);
}
