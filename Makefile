# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
CFLAGS ?= -O2
BASE = -std=c11 -Wall -Wextra -Werror -I Libraries/include
SRC = native/filesystems/nilfs2/nilfs2.c
.PHONY: all check clean
all: build/NILFS2.so build/freestanding/NILFS2.so
build/NILFS2.so: $(SRC) Makefile
	mkdir -p build
	$(CC) $(BASE) $(CFLAGS) -fPIC -fvisibility=hidden -shared -Wl,-z,defs -o $@ $(SRC)
build/freestanding/NILFS2.so: $(SRC) Makefile
	mkdir -p build/freestanding
	$(CC) $(BASE) -O2 -march=x86-64 -DIFS_FREESTANDING -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -fPIC -fvisibility=hidden -shared -nostdlib -Wl,-z,defs,-Bsymbolic -o $@ $(SRC)
build/test_nilfs2: tests/test_nilfs2.c
	mkdir -p build
	$(CC) $(BASE) $(CFLAGS) -o $@ $< -ldl
check: all build/test_nilfs2
	test "$$(nm -D --defined-only build/NILFS2.so | awk '{print $$3}')" = infiltrator_filesystem_api_v2
	test "$$(nm -D --defined-only build/freestanding/NILFS2.so | awk '{print $$3}')" = infiltrator_filesystem_api_v2
	test -z "$$(nm -D --undefined-only build/freestanding/NILFS2.so)"
	! readelf -d build/freestanding/NILFS2.so | grep -q NEEDED
	build/test_nilfs2 ./build/NILFS2.so
	build/test_nilfs2 ./build/freestanding/NILFS2.so
clean:
	rm -rf build
