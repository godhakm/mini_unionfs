#include "mini_unionfs.h"

/*
 * resolve_path()
 *
 * Translates a virtual FUSE path like "/subdir/config.txt" into the
 * real absolute path on disk that should be used for this operation.
 *
 * The `path` argument always starts with '/' (FUSE convention).
 * We strip that leading slash when appending to our base directories.
 *
 * Parameters:
 *   path       - virtual path from FUSE (e.g. "/config.txt")
 *   resolved   - output buffer, must be UNIONFS_PATH_MAX bytes
 *
 * Returns:
 *    0          - resolved is filled with the real path to use
 *   -ENOENT     - file does not exist or has been whiteout'd
 */
int resolve_path(const char *path, char *resolved)
{
    struct mini_unionfs_state *state = UNIONFS_DATA;

    /* We will build several candidate paths into these buffers. */
    char upper_path[UNIONFS_PATH_MAX];
    char lower_path[UNIONFS_PATH_MAX];
    char whiteout_path[UNIONFS_PATH_MAX];

    /*
     * Extract just the filename (basename) from the virtual path so
     * we can construct the whiteout filename.
     *
     * Example: path = "/subdir/config.txt"
     *   dirname  = "/subdir"
     *   basename = "config.txt"
     *   whiteout = "/subdir/.wh.config.txt"
     *
     * We use a copy because POSIX dirname() may modify its argument.
     */
    char path_copy[UNIONFS_PATH_MAX];
    strncpy(path_copy, path, UNIONFS_PATH_MAX - 1);
    path_copy[UNIONFS_PATH_MAX - 1] = '\0';

    /* dirname() and basename() operate on the path components.     */
    /* We need the parent directory part to place the whiteout in   */
    /* the correct subdirectory inside upper_dir.                   */
    const char *base = strrchr(path, '/');
    if (base == NULL) {
        /* Should never happen since FUSE always gives us '/' prefix */
        return -ENOENT;
    }
    base++; /* skip the '/' to get just the filename component      */

    /* Build the directory portion of the virtual path.
     * e.g. path="/subdir/config.txt" → dir_part="/subdir"
     *      path="/config.txt"        → dir_part=""              */
    char dir_part[UNIONFS_PATH_MAX];
    size_t dir_len = (size_t)(base - path - 1); /* length before last '/' */
    if (dir_len == 0) {
        dir_part[0] = '\0'; /* file is at the root of the mount    */
    } else {
        strncpy(dir_part, path + 1, dir_len - 1); /* skip leading '/' */
        dir_part[dir_len - 1] = '\0';
    }

    /* ----------------------------------------------------------------
     * Step 1: Check for a whiteout file in upper_dir.
     *
     * Whiteout path = upper_dir / <dir_part> / .wh.<basename>
     *
     * If it exists, this file has been "deleted" from the user's
     * perspective. Return -ENOENT so callers treat it as missing.
     * ---------------------------------------------------------------- */
    if (strlen(dir_part) > 0) {
        snprintf(whiteout_path, UNIONFS_PATH_MAX, "%s/%s/%s%s",
                 state->upper_dir, dir_part, WHITEOUT_PREFIX, base);
    } else {
        snprintf(whiteout_path, UNIONFS_PATH_MAX, "%s/%s%s",
                 state->upper_dir, WHITEOUT_PREFIX, base);
    }

    if (access(whiteout_path, F_OK) == 0) {
        /* Whiteout exists — the file is considered deleted.        */
        return -ENOENT;
    }

    /* ----------------------------------------------------------------
     * Step 2: Check upper_dir for the real file.
     *
     * The upper layer always takes precedence over the lower layer.
     * This is what makes CoW work: once a file is copied up to
     * upper_dir, all subsequent reads and writes go there.
     * ---------------------------------------------------------------- */
    snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s",
             state->upper_dir, path);

    if (access(upper_path, F_OK) == 0) {
        strncpy(resolved, upper_path, UNIONFS_PATH_MAX - 1);
        resolved[UNIONFS_PATH_MAX - 1] = '\0';
        return 0;
    }

    /* ----------------------------------------------------------------
     * Step 3: Fall back to lower_dir.
     *
     * The lower layer is read-only — callers must NOT write here.
     * If a write is needed, Member 3's CoW logic will copy the file
     * to upper_dir first, and future resolve_path calls will then
     * hit Step 2 instead of this step.
     * ---------------------------------------------------------------- */
    snprintf(lower_path, UNIONFS_PATH_MAX, "%s%s",
             state->lower_dir, path);

    if (access(lower_path, F_OK) == 0) {
        strncpy(resolved, lower_path, UNIONFS_PATH_MAX - 1);
        resolved[UNIONFS_PATH_MAX - 1] = '\0';
        return 0;
    }

    /* ----------------------------------------------------------------
     * Step 4: Not found anywhere.
     * ---------------------------------------------------------------- */
    return -ENOENT;
}
