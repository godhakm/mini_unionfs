# ---------------------------------------------------------------
# Mini-UnionFS Makefile
# Requires: libfuse3-dev, fuse3, pkg-config
# Install:  sudo apt install libfuse3-dev fuse3 pkg-config
# ---------------------------------------------------------------

TARGET   = mini_unionfs

# Source files — each team member adds their .c file here
SRCS     = main.c \
           resolve_path.c \
           readdir_read.c \
           write_create.c \
           unlink_whiteout.c

OBJS     = $(SRCS:.c=.o)

# Compiler settings
CC       = gcc
CFLAGS   = -Wall -Wextra -g $(shell pkg-config --cflags fuse3)
LDFLAGS  = $(shell pkg-config --libs fuse3)

# ---------------------------------------------------------------
# Default target: build the binary
# ---------------------------------------------------------------
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile each .c into a .o
%.o: %.c mini_unionfs.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------
# clean: remove build artifacts
# ---------------------------------------------------------------
clean:
	rm -f $(OBJS) $(TARGET)

# ---------------------------------------------------------------
# test: run the automated test suite from the project spec
# Assumes test_unionfs.sh is in the same directory
# ---------------------------------------------------------------
test: all
	chmod +x test_unionfs.sh
	./test_unionfs.sh

# ---------------------------------------------------------------
# mount: quick helper to mount with foreground + debug output
# Usage: make mount LOWER=./lower UPPER=./upper MNT=./mnt
# ---------------------------------------------------------------
mount: all
	mkdir -p $(LOWER) $(UPPER) $(MNT)
	./$(TARGET) $(LOWER) $(UPPER) $(MNT) -f

# ---------------------------------------------------------------
# umount: unmount the filesystem
# Usage: make umount MNT=./mnt
# ---------------------------------------------------------------
umount:
	fusermount -u $(MNT) || umount $(MNT)

.PHONY: all clean test mount umount
