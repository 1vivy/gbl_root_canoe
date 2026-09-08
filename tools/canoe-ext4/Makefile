CC ?= cc
PKG_CONFIG ?= pkg-config
CPPFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror
EXT2FS_CFLAGS := $(shell $(PKG_CONFIG) --cflags ext2fs 2>/dev/null)
EXT2FS_LIBS := $(shell $(PKG_CONFIG) --libs ext2fs 2>/dev/null)
ifeq ($(strip $(EXT2FS_LIBS)),)
EXT2FS_LIBS := -lext2fs
endif
LDLIBS += $(EXT2FS_LIBS) -lcom_err -luuid
LDFLAGS += -Wl,-z,relro,-z,now

.PHONY: all clean test windows static

all: canoe-ext4

canoe-ext4: canoe-ext4.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXT2FS_CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

# Release helpers must not depend on the recipient's libext2fs ABI. Keep a
# separate output so a previous dynamic development build cannot be packaged.
static: canoe-ext4-static

canoe-ext4-static: canoe-ext4.c Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXT2FS_CFLAGS) $(LDFLAGS) -static -o $@ $< $(LDLIBS)
	! readelf -d $@ | grep -q '(NEEDED)'

test: canoe-ext4
	sh tests/run.sh

windows:
	sh build-windows.sh

clean:
	rm -f canoe-ext4 canoe-ext4-static
