#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BINARY="$PROJECT_ROOT/mini_unionfs"
WORKDIR=$(mktemp -d -t mini_unionfs.XXXXXX)
LOWER="$WORKDIR/lower"
UPPER="$WORKDIR/upper"
MOUNT="$WORKDIR/mnt"
LOGFILE="$WORKDIR/mini_unionfs.log"
FUSE_PID=""

cleanup() {
  local exit_status=$?
  if [[ -n "$FUSE_PID" ]] && kill -0 "$FUSE_PID" 2>/dev/null; then
    kill "$FUSE_PID" 2>/dev/null || true
  fi
  if is_mounted; then
    fusermount -u "$MOUNT" 2>/dev/null || umount "$MOUNT" 2>/dev/null || true
  fi
  rm -rf "$WORKDIR"
  exit $exit_status
}

trap cleanup EXIT

is_mounted() {
  mountpoint -q "$MOUNT" 2>/dev/null || grep -qs " $MOUNT " /proc/mounts 2>/dev/null
}

build_binary() {
  echo "[build] Compiling mini_unionfs..."
  make -C "$PROJECT_ROOT" >/dev/null
}

prepare_layers() {
  mkdir -p "$LOWER" "$UPPER" "$MOUNT"
  printf 'lower_only\n' > "$LOWER/lower_only.txt"
  printf 'base_content\n' > "$LOWER/edit_me.txt"
  printf 'please_delete\n' > "$LOWER/delete_me.txt"
  mkdir -p "$LOWER/shared/sub"
  printf 'nested\n' > "$LOWER/shared/sub/nested.txt"
}

start_unionfs() {
  echo "[run] Launching mini_unionfs..."
  "$BINARY" "$LOWER" "$UPPER" "$MOUNT" -f -s >"$LOGFILE" 2>&1 &
  FUSE_PID=$!
  for _ in {1..50}; do
    if is_mounted; then
      return
    fi
    if ! kill -0 "$FUSE_PID" 2>/dev/null; then
      cat "$LOGFILE" >&2 || true
      echo "mini_unionfs exited unexpectedly" >&2
      exit 1
    fi
    sleep 0.1
  done
  echo "Timed out waiting for mount" >&2
  exit 1
}

run_test() {
  local name=$1
  shift
  printf "%-35s" "$name"
  if "$@"; then
    echo "PASSED"
  else
    echo "FAILED"
    exit 1
  fi
}

test_layer_visibility() {
  [[ -f "$MOUNT/lower_only.txt" ]] || return 1
  local content
  content=$(cat "$MOUNT/lower_only.txt")
  [[ "$content" == "lower_only" ]]
}

test_copy_on_write() {
  local before after
  before=$(cat "$LOWER/edit_me.txt")
  printf 'appended_line\n' >> "$MOUNT/edit_me.txt"
  after=$(cat "$LOWER/edit_me.txt")
  [[ "$before" == "$after" ]] || return 1
  grep -q 'appended_line' "$UPPER/edit_me.txt"
}

test_whiteout_mechanism() {
  rm "$MOUNT/delete_me.txt"
  [[ ! -e "$MOUNT/delete_me.txt" ]]
  [[ -f "$LOWER/delete_me.txt" ]]
  [[ -f "$UPPER/.wh.delete_me.txt" ]]
}

main() {
  build_binary
  prepare_layers
  start_unionfs
  run_test "Test 1: Layer Visibility" test_layer_visibility
  run_test "Test 2: Copy-on-Write" test_copy_on_write
  run_test "Test 3: Whiteout mechanism" test_whiteout_mechanism
  echo "All Mini-UnionFS tests passed."
}

main
