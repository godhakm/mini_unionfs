# Mini-UnionFS Demo Script

Use this script during your presentation to demonstrate every feature: visibility across layers, copy-on-write edits, directory merging, creation of new entries, and whiteouts.

## 0. Setup (before the session)
```bash
sudo apt update
sudo apt install -y build-essential pkg-config fuse3 libfuse3-dev
make
mkdir -p lower upper mnt
cat <<'DATA' > lower/base.txt
base_only
DATA
cat <<'DATA' > lower/edit_me.txt
to be edited
DATA
echo "delete me" > lower/delete_me.txt
mkdir -p lower/shared/sub
echo "nested" > lower/shared/sub/nested.txt
```

## 1. Start Mini-UnionFS
Terminal A:
```bash
./mini_unionfs ./lower ./upper ./mnt -f
```
Explain the arguments (lower base, upper writable, mount target) and that `-f` keeps logs in the foreground.

## 2. Layer Visibility
Terminal B:
```bash
ls mnt
cat mnt/base.txt
```
Call out that files appear in `mnt/` even though they only exist in `lower/`.

## 3. Copy-on-Write Edit
```bash
echo "new line" >> mnt/edit_me.txt
cat upper/edit_me.txt   # shows both lines
cat lower/edit_me.txt   # remains original
```
Explain: editing through `mnt/` copied the file up first, so the lower layer stays pristine.

## 4. Creating Files and Directories
```bash
echo "custom" > mnt/custom.txt
mkdir -p mnt/mydir
ls upper
```
Point out the new entries only live in `upper/`.

## 5. Directory Merge & Overrides
```bash
echo "from lower" > lower/shared/file.txt
echo "from upper" > upper/shared/file.txt
ls mnt/shared
cat mnt/shared/file.txt
```
Upper version takes precedence when names overlap.

## 6. Whiteout Deletion
```bash
rm mnt/delete_me.txt
ls -a upper | grep wh
ls mnt | grep delete_me  # nothing shown
ls lower/delete_me.txt   # original still there
```
Explain whiteouts: `.wh.delete_me.txt` hides the lower file without deleting it.

## 7. Nested Whiteout Example
```bash
rm mnt/shared/sub/nested.txt
find upper -name '*.wh*'
```
Shows that whiteouts are created inside matching subdirectories.

## 8. Readdir Merge Recap
```bash
ls mnt
```
Mention that the listing combines upper entries first, then lower ones unless masked.

## 9. Teardown
```bash
fusermount -u ./mnt   # or umount ./mnt on macOS/BSD
```
Optional: show `ls upper` afterward to recap which files/whiteouts were created.

## Optional Automated Test
If `test_unionfs.sh` is available:
```bash
chmod +x test_unionfs.sh
./test_unionfs.sh
```
Highlights the automated PASS/FAIL checks for visibility, copy-on-write, and whiteout logic.
