#include "../mini_unionfs.h"

/*
 * Member 2: Read operations + directory listing.
 *
 * This file is intentionally self-contained and avoids assumptions about
 * the rest of the codebase beyond the shared contract in mini_unionfs.h:
 *   - struct mini_unionfs_state contains lower_dir and upper_dir
 *   - resolve_path(path, resolved) implements unionfs resolution rules
 *   - WHITEOUT_PREFIX is the whiteout naming convention (".wh.")
 *   - UNIONFS_PATH_MAX is a safe path buffer size
 *
 * Portability notes:
 *   - Linux/WSL typically builds against libfuse3 (FUSE3 API)
 *   - macOS typically uses macFUSE, which is FUSE 2.x compatible
 *   - The readdir callback signature differs between FUSE2 and FUSE3.
 */

#if defined(FUSE_MAJOR_VERSION) && (FUSE_MAJOR_VERSION >= 3)
#define MINI_UNIONFS_FUSE3 1
#else
#define MINI_UNIONFS_FUSE3 0
#endif

/* -------------------------------------------------------------------------
 * Small helper: call filler with the right arity across FUSE versions.
 * ------------------------------------------------------------------------- */
#if MINI_UNIONFS_FUSE3
static inline int fill_dir(void *buf, fuse_fill_dir_t filler, const char *name)
{
    return filler(buf, name, NULL, 0, (enum fuse_fill_dir_flags)0);
}
#else
static inline int fill_dir(void *buf, fuse_fill_dir_t filler, const char *name)
{
    return filler(buf, name, NULL, 0);
}
#endif

/* -------------------------------------------------------------------------
 * A tiny "seen names" set.
 *
 * We keep it intentionally simple (linear search) because directories in
 * teaching projects are small, and this avoids non-portable hash APIs.
 * ------------------------------------------------------------------------- */
struct seen_set {
    char **names;
    size_t count;
    size_t cap;
};

static void seen_init(struct seen_set *set)
{
    set->names = NULL;
    set->count = 0;
    set->cap = 0;
}

static void seen_free(struct seen_set *set)
{
    if (set->names == NULL) return;
    for (size_t i = 0; i < set->count; i++) {
        free(set->names[i]);
    }
    free(set->names);
    set->names = NULL;
    set->count = 0;
    set->cap = 0;
}

static int seen_contains(const struct seen_set *set, const char *name)
{
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->names[i], name) == 0) return 1;
    }
    return 0;
}

static int seen_add(struct seen_set *set, const char *name)
{
    if (seen_contains(set, name)) return 0;

    if (set->count == set->cap) {
        size_t new_cap = (set->cap == 0) ? 32 : (set->cap * 2);
        char **new_names = realloc(set->names, new_cap * sizeof(char *));
        if (new_names == NULL) return -ENOMEM;
        set->names = new_names;
        set->cap = new_cap;
    }

    set->names[set->count] = strdup(name);
    if (set->names[set->count] == NULL) return -ENOMEM;

    set->count++;
    return 0;
}

/*
 * Returns 1 if the directory entry should be hidden from the merged view.
 * We hide the internal whiteout marker files from the user.
 */
static int is_internal_whiteout_name(const char *name)
{
    return (strncmp(name, WHITEOUT_PREFIX, strlen(WHITEOUT_PREFIX)) == 0);
}

/*
 * Compute the on-disk path to a possible whiteout marker for a name inside
 * the directory represented by `path`.
 *
 * `path` is the FUSE virtual directory path (e.g. "/", "/etc").
 * Whiteout should be in the *upper layer* within that directory.
 */
static void build_whiteout_path(char out[UNIONFS_PATH_MAX],
                                const struct mini_unionfs_state *state,
                                const char *path,
                                const char *name)
{
    char upper_dir_path[UNIONFS_PATH_MAX];
    snprintf(upper_dir_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);

    /* Always inject a '/' separator; double slashes are harmless. */
    snprintf(out, UNIONFS_PATH_MAX, "%s/%s%s", upper_dir_path, WHITEOUT_PREFIX, name);
}

/* -------------------------------------------------------------------------
 * unionfs_open()
 *
 * Called when a file is opened. For this mini filesystem we only need to
 * validate that the path resolves (i.e., exists and isn't whiteout'd).
 * We don't keep file handles here — read() will open+pread as needed.
 * ------------------------------------------------------------------------- */
int unionfs_open(const char *path, struct fuse_file_info *fi)
{
    (void)fi;

    char resolved[UNIONFS_PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0) return res;

    return 0;
}

/* -------------------------------------------------------------------------
 * unionfs_read()
 *
 * Called to read file contents.
 * Must use pread() because FUSE may request reads at arbitrary offsets.
 * ------------------------------------------------------------------------- */
int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi)
{
    (void)fi;

    char resolved[UNIONFS_PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0) return res;

    int fd = open(resolved, O_RDONLY);
    if (fd == -1) return -errno;

    ssize_t bytes = pread(fd, buf, size, offset);
    if (bytes == -1) bytes = -errno;

    close(fd);
    return (int)bytes;
}

/* -------------------------------------------------------------------------
 * unionfs_readdir()
 *
 * Called when listing a directory (e.g. `ls`). We must merge entries from:
 *   - upper layer directory
 *   - lower layer directory
 * with these rules:
 *   - upper entries take precedence
 *   - hide any lower entry that has a corresponding whiteout marker in upper
 *   - do not show internal whiteout files to the user
 * ------------------------------------------------------------------------- */
#if MINI_UNIONFS_FUSE3
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags)
#else
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi)
#endif
{
    (void)offset;
    (void)fi;
#if MINI_UNIONFS_FUSE3
    (void)flags;
#endif

    struct mini_unionfs_state *state = UNIONFS_DATA;

    /* Always add the mandatory dot entries first. */
    if (fill_dir(buf, filler, ".") != 0) return 0;
    if (fill_dir(buf, filler, "..") != 0) return 0;

    /* Build directory paths in both layers. */
    char upper_dir_path[UNIONFS_PATH_MAX];
    char lower_dir_path[UNIONFS_PATH_MAX];
    snprintf(upper_dir_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);
    snprintf(lower_dir_path, UNIONFS_PATH_MAX, "%s%s", state->lower_dir, path);

    struct seen_set seen;
    seen_init(&seen);

    /* 1) Add entries from upper layer (if directory exists). */
    DIR *dp = opendir(upper_dir_path);
    if (dp != NULL) {
        errno = 0;
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            const char *name = de->d_name;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            if (is_internal_whiteout_name(name)) continue;

            /* Track it before exposing it. */
            int add_res = seen_add(&seen, name);
            if (add_res != 0) {
                closedir(dp);
                seen_free(&seen);
                return add_res;
            }

            /* If filler returns non-zero, the buffer is full: stop. */
            if (fill_dir(buf, filler, name) != 0) break;
        }

        int saved_errno = errno;
        closedir(dp);
        if (saved_errno != 0) {
            /* readdir() error */
            seen_free(&seen);
            return -saved_errno;
        }
    } else if (errno != ENOENT && errno != ENOTDIR) {
        /* If upper dir doesn't exist, that's fine. Other errors should fail. */
        int err = -errno;
        seen_free(&seen);
        return err;
    }

    /* 2) Add entries from lower layer that are not shadowed. */
    dp = opendir(lower_dir_path);
    if (dp != NULL) {
        errno = 0;
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            const char *name = de->d_name;

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

            /* Skip if upper already provided it. */
            if (seen_contains(&seen, name)) continue;

            /* Skip if a whiteout exists in upper for this name. */
            char wh_path[UNIONFS_PATH_MAX];
            build_whiteout_path(wh_path, state, path, name);
            if (access(wh_path, F_OK) == 0) continue;

            if (fill_dir(buf, filler, name) != 0) break;
        }

        int saved_errno = errno;
        closedir(dp);
        if (saved_errno != 0) {
            seen_free(&seen);
            return -saved_errno;
        }
    } else if (errno != ENOENT && errno != ENOTDIR) {
        int err = -errno;
        seen_free(&seen);
        return err;
    }

    seen_free(&seen);
    return 0;
}
