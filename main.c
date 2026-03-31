#include "mini_unionfs.h"

/* ----------------------------------------------------------------
 * unionfs_getattr()
 *
 * Called by FUSE whenever something needs file metadata (ls, stat,
 * open, etc.). We resolve the virtual path to a real path and then
 * call lstat() to fill the stat buffer FUSE expects.
 *
 * Returns 0 on success, negative errno on failure.
 * ---------------------------------------------------------------- */
/* FUSE3: getattr includes a fuse_file_info* argument. */
#if MINI_UNIONFS_FUSE3
int unionfs_getattr(const char *path, struct stat *stbuf,
                    struct fuse_file_info *fi)
{
    (void) fi; /* unused in our implementation */

    char resolved[UNIONFS_PATH_MAX];

    /* Special case: the root directory "/" always exists.
     * It's a virtual directory, not a real file on disk.          */
    if (strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    /* Resolve the virtual path to a real path on disk.
     * If the file is whiteout'd or simply doesn't exist,
     * resolve_path returns -ENOENT and we pass that back to FUSE. */
    int res = resolve_path(path, resolved);
    if (res != 0) {
        return res; /* -ENOENT */
    }

    /* Use lstat (not stat) so we don't follow symlinks.           */
    memset(stbuf, 0, sizeof(struct stat));
    if (lstat(resolved, stbuf) == -1) {
        return -errno;
    }

    return 0;
}
#else
/* FUSE2 (macFUSE): getattr has no fuse_file_info* argument. */
int unionfs_getattr(const char *path, struct stat *stbuf)
{
    char resolved[UNIONFS_PATH_MAX];

    if (strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    int res = resolve_path(path, resolved);
    if (res != 0) {
        return res;
    }

    memset(stbuf, 0, sizeof(struct stat));
    if (lstat(resolved, stbuf) == -1) {
        return -errno;
    }

    return 0;
}
#endif

/* ----------------------------------------------------------------
 * FUSE operations table.
 *
 * Only Member 1's callbacks are filled in here.
 * The rest of the team will add their callbacks to this struct —
 * just add a new line for each one (e.g. .readdir = unionfs_readdir).
 *
 * IMPORTANT FOR THE TEAM: Do not define a second fuse_operations
 * struct in your own files. Add your function pointers here.
 * ---------------------------------------------------------------- */
static struct fuse_operations unionfs_oper = {
    .getattr  = unionfs_getattr,

    /* Member 2 fills these in: */
    .readdir  = unionfs_readdir,
    .read     = unionfs_read,
    .open     = unionfs_open,

    /* Member 3 fills these in: */
    .write    = unionfs_write,
    .create   = unionfs_create,
    .mkdir    = unionfs_mkdir,
    .rmdir    = unionfs_rmdir,

    /* Member 4 fills these in: */
    .unlink   = unionfs_unlink,
};

/* ----------------------------------------------------------------
 * usage()
 *
 * Prints how to run the program and exits.
 * ---------------------------------------------------------------- */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <lower_dir> <upper_dir> <mount_point> [FUSE options]\n"
        "\n"
        "  lower_dir    Read-only base layer directory\n"
        "  upper_dir    Read-write container layer directory\n"
        "  mount_point  Where the merged filesystem will be mounted\n"
        "\nExample:\n"
        "  %s ./lower ./upper ./mnt\n"
        "\nTo unmount:\n"
        "  fusermount -u ./mnt\n",
        prog, prog);
    exit(EXIT_FAILURE);
}

/* ----------------------------------------------------------------
 * main()
 *
 * 1. Validate that we received at least 3 arguments.
 * 2. Convert lower_dir and upper_dir to absolute paths (realpath)
 *    so they are safe to use regardless of how FUSE changes cwd.
 * 3. Store them in mini_unionfs_state.
 * 4. Pass the mount_point and any remaining FUSE flags to fuse_main.
 *
 * FUSE argument convention:
 *   argv[0] = program name  (kept as-is for FUSE)
 *   argv[1] = lower_dir     (consumed by us, removed before fuse_main)
 *   argv[2] = upper_dir     (consumed by us, removed before fuse_main)
 *   argv[3] = mount_point   (passed to FUSE)
 *   argv[4+]= FUSE options  (passed to FUSE, e.g. -f for foreground)
 * ---------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* Need at least: progname lower_dir upper_dir mount_point      */
    if (argc < 4) {
        usage(argv[0]);
    }

    /* Allocate and populate the global state.                      */
    struct mini_unionfs_state *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    /* Resolve lower_dir to an absolute path.                       */
    state->lower_dir = realpath(argv[1], NULL);
    if (state->lower_dir == NULL) {
        fprintf(stderr, "Error: lower_dir '%s': %s\n",
                argv[1], strerror(errno));
        free(state);
        return EXIT_FAILURE;
    }

    /* Resolve upper_dir to an absolute path.                       */
    state->upper_dir = realpath(argv[2], NULL);
    if (state->upper_dir == NULL) {
        fprintf(stderr, "Error: upper_dir '%s': %s\n",
                argv[2], strerror(errno));
        free(state->lower_dir);
        free(state);
        return EXIT_FAILURE;
    }

    printf("[mini_unionfs] lower_dir : %s\n", state->lower_dir);
    printf("[mini_unionfs] upper_dir : %s\n", state->upper_dir);
    printf("[mini_unionfs] mounting on: %s\n", argv[3]);

    /*
     * Rebuild argv for fuse_main, removing lower_dir and upper_dir.
     *
     * fuse_main expects:
     *   fuse_argv[0] = program name
     *   fuse_argv[1] = mount_point
     *   fuse_argv[2+]= optional FUSE flags
     *
     * Original argv layout:
     *   [0]=progname [1]=lower [2]=upper [3]=mountpoint [4+]=flags
     *
     * New fuse_argv layout:
     *   [0]=progname [1]=mountpoint [2+]=flags
     */
    int fuse_argc = argc - 2;                  /* drop lower and upper */
    char **fuse_argv = malloc(fuse_argc * sizeof(char *));
    if (fuse_argv == NULL) {
        perror("malloc");
        free(state->lower_dir);
        free(state->upper_dir);
        free(state);
        return EXIT_FAILURE;
    }

    fuse_argv[0] = argv[0];                    /* program name        */
    for (int i = 1; i < fuse_argc; i++) {
        fuse_argv[i] = argv[i + 2];            /* mountpoint + flags  */
    }

    /* Hand control to FUSE. state is passed as private_data and is
     * retrievable in any callback via UNIONFS_DATA.                 */
    int ret = fuse_main(fuse_argc, fuse_argv, &unionfs_oper, state);

    /* Cleanup (only reached after unmount).                        */
    free(fuse_argv);
    free(state->lower_dir);
    free(state->upper_dir);
    free(state);

    return ret;
}
