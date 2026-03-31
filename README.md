# Mini-UnionFS Group Project

A simplified Union File System in userspace using FUSE, simulating how Docker layers work.

## Team Structure

| Member | File | Responsibility |
|--------|------|----------------|
| Member 1 | main.c, resolve_path.c, mini_unionfs.h, Makefile | Core infrastructure (DONE) |
| Member 2 | readdir_read.c | Read operations and directory listing |
| Member 3 | write_create.c | Write operations and Copy-on-Write |
| Member 4 | unlink_whiteout.c | Deletion and whiteout mechanism |

---

## Setup for Everyone

Install WSL2 + Ubuntu (Windows only): run "wsl --install" in PowerShell as Admin, restart PC.

Inside Ubuntu/WSL:
  sudo apt update
  sudo apt install -y libfuse3-dev fuse3 pkg-config build-essential git

Clone and build:
  git clone https://github.com/godhakm/mini_unionfs.git
  cd mini_unionfs
  make

Quick test:
  mkdir -p lower upper mnt
  echo "hello from lower" > lower/test.txt
  ./mini_unionfs ./lower ./upper ./mnt -f
  # In a second WSL terminal:
  cat ~/mini_unionfs/mnt/test.txt    # prints: hello from lower
  fusermount -u ~/mini_unionfs/mnt   # unmount when done

---

## How the Codebase Works

Three directories:
  lower/   read-only base layer
  upper/   read-write layer (all changes go here)
  mnt/     the merged view the user sees

The key function is resolve_path(path, resolved). Call it at the start of every
FUSE callback. It figures out the real disk path for a virtual path:
  1. If upper/.wh.<filename> exists  -> deleted, return -ENOENT
  2. If upper/<path> exists          -> use that
  3. If lower/<path> exists          -> use that
  4. Otherwise                       -> return -ENOENT

To get lower_dir and upper_dir inside any callback:
  struct mini_unionfs_state *state = UNIONFS_DATA;
  state->lower_dir   // absolute path to lower
  state->upper_dir   // absolute path to upper

Rules:
  - Never write to lower_dir
  - Never define your own fuse_operations struct (it is in main.c)
  - Never modify mini_unionfs.h or main.c without telling the team
  - Never commit .o files or the mini_unionfs binary

---

## Member 1 - COMPLETE

Implemented: mini_unionfs.h (shared header with structs, macros, constants, all declarations),
resolve_path (core path resolution), unionfs_getattr (file metadata via lstat),
main() (argument parsing, realpath, fuse_main), and Makefile.

---

## Member 2 - readdir_read.c

Implement these three functions.

### unionfs_open
Called when a file is opened. Just resolve the path and return 0 if found.

  int unionfs_open(const char *path, struct fuse_file_info *fi)
  {
      char resolved[UNIONFS_PATH_MAX];
      int res = resolve_path(path, resolved);
      if (res != 0) return res;
      return 0;
  }

### unionfs_read
Called when file contents are needed. Use pread not read (FUSE may read at any offset).

  int unionfs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
  {
      char resolved[UNIONFS_PATH_MAX];
      int res = resolve_path(path, resolved);
      if (res != 0) return res;
      int fd = open(resolved, O_RDONLY);
      if (fd == -1) return -errno;
      int bytes = pread(fd, buf, size, offset);
      if (bytes == -1) bytes = -errno;
      close(fd);
      return bytes;
  }

### unionfs_readdir
Called when a directory is listed (ls). You must merge both layers.

Steps:
  1. Call filler(buf, ".", NULL, 0, 0) and filler(buf, "..", NULL, 0, 0) first
  2. Open upper_dir/path with opendir/readdir. Add each entry with filler. Track names added.
  3. Open lower_dir/path with opendir/readdir. For each entry:
     - Skip "." and ".."
     - Skip if already added from upper
     - Skip if a whiteout exists in upper for it:
         char wh[UNIONFS_PATH_MAX];
         snprintf(wh, UNIONFS_PATH_MAX, "%s%s/.wh.%s", state->upper_dir, path, entry->d_name);
         if (access(wh, F_OK) == 0) continue;
     - Otherwise add with filler

Track seen names like this:
  char seen[256][NAME_MAX + 1];
  int seen_count = 0;
  // after adding from upper: strncpy(seen[seen_count++], entry->d_name, NAME_MAX);
  // before adding from lower: loop through seen[], skip if match

Test to pass: Test 1 (Layer Visibility) in test_unionfs.sh

---

## Member 3 - write_create.c

Implement these four functions.

### Copy-on-Write helper (write this first)
If a user edits a lower_dir file, copy it to upper_dir before writing. Never touch lower_dir.

  static int cow_copy(const char *path)
  {
      struct mini_unionfs_state *state = UNIONFS_DATA;
      char lower_path[UNIONFS_PATH_MAX], upper_path[UNIONFS_PATH_MAX];
      snprintf(lower_path, UNIONFS_PATH_MAX, "%s%s", state->lower_dir, path);
      snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);

      int src = open(lower_path, O_RDONLY);
      if (src == -1) return -errno;
      int dst = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      if (dst == -1) { close(src); return -errno; }

      char buf[4096];
      ssize_t n;
      while ((n = read(src, buf, sizeof(buf))) > 0)
          write(dst, buf, n);

      close(src); close(dst);
      return 0;
  }

### unionfs_write
  1. Call resolve_path to find current location of file
  2. If it is in lower_dir, call cow_copy(path) first
  3. Open upper_dir version with O_WRONLY
  4. Use pwrite(fd, buf, size, offset)
  5. Return bytes written

  int unionfs_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi)
  {
      struct mini_unionfs_state *state = UNIONFS_DATA;
      char resolved[UNIONFS_PATH_MAX], upper_path[UNIONFS_PATH_MAX];
      resolve_path(path, resolved);
      snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);
      if (strncmp(resolved, state->lower_dir, strlen(state->lower_dir)) == 0) {
          int res = cow_copy(path);
          if (res != 0) return res;
      }
      int fd = open(upper_path, O_WRONLY);
      if (fd == -1) return -errno;
      int bytes = pwrite(fd, buf, size, offset);
      if (bytes == -1) bytes = -errno;
      close(fd);
      return bytes;
  }

### unionfs_create
New files always go to upper_dir:
  snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);
  int fd = open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
  if (fd == -1) return -errno;
  close(fd); return 0;

### unionfs_mkdir
New dirs always go to upper_dir:
  snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);
  if (mkdir(upper_path, mode) == -1) return -errno;
  return 0;

### unionfs_rmdir
If dir is in upper_dir: rmdir(upper_path).
If only in lower_dir: create a whiteout marker in upper_dir.

Test to pass: Test 2 (Copy-on-Write) in test_unionfs.sh

---

## Member 4 - unlink_whiteout.c

Implement one function: unionfs_unlink.

### unionfs_unlink
Called when a file is deleted with rm. Two cases:

Case 1 - file is in upper_dir: physically delete it.
  if (access(upper_path, F_OK) == 0) {
      if (unlink(upper_path) == -1) return -errno;
      return 0;
  }

Case 2 - file is only in lower_dir: create a whiteout in upper_dir.
  Deleting /config.txt creates upper_dir/.wh.config.txt (an empty file).
  The whiteout file's presence tells resolve_path to hide the original.

  int fd = open(whiteout_path, O_CREAT | O_WRONLY, 0644);
  if (fd == -1) return -errno;
  close(fd); return 0;

Full implementation:
  int unionfs_unlink(const char *path)
  {
      struct mini_unionfs_state *state = UNIONFS_DATA;
      char upper_path[UNIONFS_PATH_MAX];
      snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);

      if (access(upper_path, F_OK) == 0) {
          if (unlink(upper_path) == -1) return -errno;
          return 0;
      }

      const char *base = strrchr(path, '/'); base++;
      size_t dir_len = (size_t)(base - path - 1);
      char dir_part[UNIONFS_PATH_MAX];
      if (dir_len == 0) { dir_part[0] = '\0'; }
      else { strncpy(dir_part, path + 1, dir_len - 1); dir_part[dir_len - 1] = '\0'; }

      char whiteout_path[UNIONFS_PATH_MAX];
      if (strlen(dir_part) > 0)
          snprintf(whiteout_path, UNIONFS_PATH_MAX, "%s/%s/.wh.%s", state->upper_dir, dir_part, base);
      else
          snprintf(whiteout_path, UNIONFS_PATH_MAX, "%s/.wh.%s", state->upper_dir, base);

      int fd = open(whiteout_path, O_CREAT | O_WRONLY, 0644);
      if (fd == -1) return -errno;
      close(fd); return 0;
  }

Verify manually:
  echo "bye" > lower/delete_me.txt
  ./mini_unionfs ./lower ./upper ./mnt -f
  # in second terminal:
  rm mnt/delete_me.txt
  ls mnt/       # delete_me.txt gone
  ls lower/     # delete_me.txt still here
  ls -la upper/ # .wh.delete_me.txt exists

Test to pass: Test 3 (Whiteout) in test_unionfs.sh

---

## Full Test Suite

Once everyone has pushed:
  chmod +x test_unionfs.sh
  ./test_unionfs.sh

Expected:
  Test 1: Layer Visibility...   PASSED
  Test 2: Copy-on-Write...      PASSED
  Test 3: Whiteout mechanism... PASSED

---

## Git Workflow

First time:
  git clone https://github.com/godhakm/mini_unionfs.git
  cd mini_unionfs

Every time you work:
  git pull origin main
  # edit your file
  git add yourfile.c
  git commit -m "Member X: description"
  git push origin main

Merge conflict:
  git pull origin main   # flags conflict
  # fix the file manually
  git add yourfile.c
  git commit -m "resolve conflict"
  git push origin main

Never commit: *.o files, mini_unionfs binary, lower/ upper/ mnt/ folders.
