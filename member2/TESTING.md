# Member 2 (readdir/read/open) — testing notes

This folder contains only Member 2's implementation: `readdir_read.c`.

## How to use it in the main project

1. Replace the project’s top-level `readdir_read.c` with this file’s contents (or copy this file over).
2. Build using the project’s normal build steps.

## Runtime sanity checks (works the same on Linux/WSL and macOS)

Assuming you have the full project wired up (Member 1 state + resolve_path, plus other members’ ops can remain unimplemented for basic read tests):

1. Create directories:

   - `mkdir -p lower upper mnt`
   - `echo "hello" > lower/a.txt`
   - `mkdir -p lower/sub && echo "world" > lower/sub/b.txt`

2. Mount:

   - `./mini_unionfs ./lower ./upper ./mnt -f`

    Notes:
    - `./mini_unionfs` must be a compiled executable in the current directory.
       If you see `zsh: no such file or directory: ./mini_unionfs`, run `make` first
       (and ensure your team’s `mini_unionfs.h` + core files are present).
    - Do NOT run `./lower ./upper ./mnt -f` — `lower/` is a directory, not a program.
       That’s why you saw `zsh: permission denied: ./lower`.

3. In a second terminal:

   - `ls mnt` (should show `a.txt` and `sub`)
   - `cat mnt/a.txt` (should print `hello`)
   - `cat mnt/sub/b.txt` (should print `world`)

4. Unmount:

   - Linux/WSL: `fusermount -u ./mnt`
   - macOS: `umount ./mnt`

## Prerequisites (high-level)

- Linux/WSL: install `libfuse3-dev` / `fuse3` and build with the project `Makefile`.
- macOS: install macFUSE and ensure your build can find FUSE headers/libs.

This folder only contains Member 2’s code; your full repo still needs the shared
header (`mini_unionfs.h`) and core infrastructure from your teammates to build.

## Notes

- This implementation hides `.wh.*` whiteout marker files from directory listings.
- It merges upper + lower directory entries, with upper taking priority.
- It also hides lower entries that have a corresponding whiteout marker in upper.
