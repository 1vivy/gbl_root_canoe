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
LDLIBS += $(EXT2FS_LIBS) -lcom_err
LDFLAGS += -Wl,-z,relro,-z,now

.PHONY: all clean test windows

all: canoe-ext4

canoe-ext4: canoe-ext4.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXT2FS_CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

# Static libext2fs builds can be requested by supplying STATIC=1.  The
# distribution's libcom_err and libc may still remain dynamic on glibc hosts.
static: LDFLAGS += -static
static: canoe-ext4

test: canoe-ext4
	sh tests/run.sh

windows:
	sh build-windows.sh

clean:
	rm -f canoe-ext4
