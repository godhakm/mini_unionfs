# Mini-UnionFS — Group Project

A simplified Union File System implemented in userspace using FUSE (Filesystem in Userspace).  
This project simulates how Docker layers work — stacking a read-write layer on top of read-only base layers.

---

## Team Structure

| Member | File(s) to implement | Responsibility |
|--------|----------------------|----------------|
| Member 1 | `main.c`, `resolve_path.c`, `mini_unionfs.h`, `Makefile` | Core infrastructure, path resolution, getattr |
| Member 2 | `readdir_read.c` | Read operations and directory listing |
| Member 3 | `write_create.c` | Write operations, Copy-on-Write, mkdir/rmdir |
| Member 4 | `unlink_whiteout.c` | Deletion and whiteout mechanism |

---

## Getting Started (everyone must do this)

### 1. Install WSL2 + Ubuntu (if on Windows)
```bash
# In PowerShell as Administrator
wsl --install
```

### 2. Install dependencies (inside Ubuntu/WSL)
```bash
sudo apt update
sudo apt install -y libfuse3-dev fuse3 pkg-config build-essential git
```

### 3. Clone the repo
```bash
git clone https://github.com/godhakm/mini_unionfs.git
cd mini_unionfs
```

### 4. Build
```bash
make
```

### 5. Test
```bash
mkdir -p lower upper mnt
echo "hello from lower" > lower/test.txt
./mini_unionfs ./lower ./upper ./mnt -f
# In a second terminal:
cat ~/mini_unionfs/mnt/test.txt   # should print: hello from lower
# Unmount when done:
fusermount -u ~/mini_unionfs/mnt
```

---

## How the Codebase Works

### Key concept: layers
- `lower_dir` — read-only base layer (like a Docker image layer)
- `upper_dir` — read-write container layer (your changes go here)
- `mnt/` — the merged virtual filesystem the user sees

### Key files

**`mini_unionfs.h`** — shared header, include this in every `.c` file  
Contains the global state struct, macros, and all function declarations.

**`resolve_path(path, resolved)`** — the most important function in the project.  
Call this at the start of every FUSE callback to turn a virtual path into a real path on disk.  
Resolution order:
1. If `upper/.wh.<filename>` exists → file is deleted, returns `-ENOENT`
2. If `upper/<path>` exists → returns that path
3. If `lower/<path>` exists → returns that path
4. Otherwise → returns `-ENOENT`

**`UNIONFS_DATA`** — macro to get the global state (lower_dir, upper_dir) inside any callback:
```c
struct mini_unionfs_state *state = UNIONFS_DATA;
// state->lower_dir  →  absolute path to lower directory
// state->upper_dir  →  absolute path to upper directory
```

---

## Member 2 — `readdir_read.c`

You implement three functions. Replace the stubs currently in this file.

### `unionfs_open(path, fi)`
Called when a file is opened for **reading**.
- Call `resolve_path(path, resolved)` — if it returns non-zero, return that error
- If the file is found, return `0` (success)
- Do NOT implement write/CoW logic here — that's Member 3's job in the write path

```c
int unionfs_open(const char *path, struct fuse_file_info *fi)
{
    char resolved[UNIONFS_PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res != 0) return res;
    return 0;
}
```

### `unionfs_read(path, buf, size, offset, fi)`
Called when file contents are requested.
- Call `resolve_path` to get the real path
- Open the real file with `open(resolved, O_RDONLY)`
- Use `pread(fd, buf, size, offset)` to read — NOT `read()`, because FUSE may call this at any offset
- Close the fd and return the number of bytes read, or `-errno` on error

### `unionfs_readdir(path, buf, filler, offset, fi, flags)`
Called when a directory is listed (e.g. `ls`). This is the most complex function you write.  
You must merge the contents of both `lower_dir` and `upper_dir`:

Steps:
1. Always call `filler(buf, ".", NULL, 0, 0)` and `filler(buf, "..", NULL, 0, 0)` first
2. Open and read `upper_dir/path` with `opendir`/`readdir` — add each entry with `filler`
3. Open and read `lower_dir/path` with `opendir`/`readdir` — for each entry:
   - Skip it if a file with the same name was already added from upper (use a seen-names tracker)
   - Skip it if a whiteout file `upper/.wh.<filename>` exists for it
   - Otherwise add it with `filler`

To check for a whiteout inside readdir:
```c
char whiteout[UNIONFS_PATH_MAX];
snprintf(whiteout, UNIONFS_PATH_MAX, "%s%s/.wh.%s",
         state->upper_dir, path, entry_name);
if (access(whiteout, F_OK) == 0) continue; // skip — it's been deleted
```

---

## Member 3 — `write_create.c`

You implement four functions. The most important is `unionfs_write` with Copy-on-Write.

### Copy-on-Write (CoW) — the core concept
If the user modifies a file that only exists in `lower_dir`, you must:
1. Copy the file from `lower_dir` to `upper_dir` first
2. Apply the write to the `upper_dir` copy
3. Never touch `lower_dir`

Write a helper function `cow_copy(path)` that does this copy. Call it from `unionfs_write` when needed.

```c
// Pseudocode for cow_copy
static int cow_copy(const char *path) {
    // 1. Build lower path and upper path from state->lower_dir / state->upper_dir
    // 2. Open lower file for reading
    // 3. Create upper file for writing (create parent dirs if needed with mkdir -p logic)
    // 4. Copy contents in a loop using read/write
    // 5. Close both files
    // 6. Return 0 on success, -errno on failure
}
```

### `unionfs_write(path, buf, size, offset, fi)`
- Call `resolve_path` to find where the file currently lives
- If it resolves to `lower_dir` → call `cow_copy(path)` first
- Then write to the `upper_dir` version using `pwrite(fd, buf, size, offset)`
- Return number of bytes written

### `unionfs_create(path, mode, fi)`
- New files always go to `upper_dir`
- Build the upper path: `upper_dir + path`
- Create parent directories if needed
- Call `open(upper_path, O_CREAT | O_WRONLY | O_TRUNC, mode)`
- Return `0` on success

### `unionfs_mkdir(path, mode)`
- Create the directory in `upper_dir` only: `mkdir(upper_path, mode)`

### `unionfs_rmdir(path)`
- If directory exists in `upper_dir` → `rmdir(upper_path)`
- If directory exists only in `lower_dir` → create a whiteout marker directory in `upper_dir`

---

## Member 4 — `unlink_whiteout.c`

You implement one function, but it needs careful coordination with Members 1 and 2.

### `unionfs_unlink(path)`
Called when a file is deleted (`rm file`).

Two cases:

**Case 1 — file is in `upper_dir`:**
- Physically delete it: `unlink(upper_path)`
- Done, return `0`

**Case 2 — file is in `lower_dir`:**
- Do NOT touch `lower_dir`
- Instead, create a whiteout file in `upper_dir`: `upper_dir/.wh.<filename>`
- Example: deleting `/config.txt` → create `upper_dir/.wh.config.txt`
- Create it with: `open(whiteout_path, O_CREAT | O_WRONLY, 0644)` then `close()`

```c
int unionfs_unlink(const char *path)
{
    struct mini_unionfs_state *state = UNIONFS_DATA;

    // Build upper path
    char upper_path[UNIONFS_PATH_MAX];
    snprintf(upper_path, UNIONFS_PATH_MAX, "%s%s", state->upper_dir, path);

    if (access(upper_path, F_OK) == 0) {
        // File is in upper — just delete it
        if (unlink(upper_path) == -1) return -errno;
        return 0;
    }

    // File is in lower — create a whiteout in upper
    // Extract the directory part and filename to build: upper/dir/.wh.filename
    // Then: int fd = open(whiteout_path, O_CREAT | O_WRONLY, 0644);
    //        close(fd);
    //        return 0;
}
```

After implementing, verify:
- `rm mnt/somefile` → `upper/.wh.somefile` exists, `lower/somefile` still exists, file is gone from `mnt/`
- This is exactly what Test 3 in `test_unionfs.sh` checks

---

## Git Workflow for the Team

### First time setup
```bash
git clone https://github.com/godhakm/mini_unionfs.git
cd mini_unionfs
```

### Each time you work
```bash
git pull origin main                  # get latest changes
# ... edit your file ...
git add yourfile.c
git commit -m "Member X: description of what you did"
git push origin main
```

### If you get a merge conflict
```bash
git pull origin main                  # this will flag the conflict
# open the conflicting file, fix it manually
git add conflicting_file.c
git commit -m "resolve merge conflict"
git push origin main
```

---

## Running the Test Suite

Once all members have pushed their implementations, run the official test:

```bash
chmod +x test_unionfs.sh
./test_unionfs.sh
```

All 3 tests must pass:
- ✅ Test 1: Layer Visibility — lower layer files are visible through the mount
- ✅ Test 2: Copy-on-Write — modifying a lower file copies it to upper, lower is untouched
- ✅ Test 3: Whiteout — deleting a lower file creates a whiteout, lower file still exists

---

## Important Rules

- **Never write to `lower_dir` directly** — it is read-only by convention
- **Always call `resolve_path` first** in every FUSE callback
- **Do not define your own `fuse_operations` struct** — it is already defined in `main.c`
- **Do not modify `mini_unionfs.h` or `main.c`** without telling the team
- **Build artifacts do not belong in git** — never commit `.o` files or the `mini_unionfs` binary
