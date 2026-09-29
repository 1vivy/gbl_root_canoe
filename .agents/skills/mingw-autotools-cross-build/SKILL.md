---
name: mingw-autotools-cross-build
description: "Cross-build autotools C projects for x86_64 Windows with MinGW when configure, linking, or artifact production fails."
---

# MinGW autotools cross-build triage

Use when `--host=x86_64-w64-mingw32` configure/make fails, or a build script "succeeds" without producing a `.exe`. Verified end to end on e2fsprogs 1.47.3 → `canoe-ext4.exe` (PE32+, 1.78 MB).

## Rule zero: a script that exits 0 with no binary is not success

`build-windows.sh` deliberately prints a message and exits 0 when MinGW or `E2FSPROGS_SRC` is absent. Guard the consuming step with `test -f <out>.exe`; otherwise the failure surfaces at packaging, far from the cause.

## The four failure classes, in the order they bite

### 1. configure runs build-machine probes with the cross compiler
Symptom: `./asm_types: No such file or directory`, and the build dir contains `asm_types.exe`. The probe was compiled for Windows and cannot be executed.

Fix: declare the build machine, don't just set `BUILD_CC`:
```sh
BUILD_CC="${BUILD_CC:-cc}" "$SOURCE/configure" \
  --build="$(cc -dumpmachine)" --host=x86_64-w64-mingw32 ...
```
Many packages derive `BUILD_CC` themselves from `--build`; an env var alone is overridden.

### 2. Missing system libraries that MinGW simply does not have
Symptom: `configure: error: external uuid library not found`, then the same for blkid, one after another as you fix each.

Fix: enable the package's **bundled** copies instead of disabling them — `--enable-libuuid --enable-libblkid`. Disabling a subsystem often still requires the external lib; enabling the vendored one removes the dependency entirely.

### 3. Make targets that name libtool artifacts the package never builds
Symptom: `No rule to make target 'lib/et/libcom_err.la'`.

Fix: check what the tree actually produces. Non-libtool autotools projects build plain archives:
```sh
make -C "$BUILD/lib/et"; make -C "$BUILD/lib/ext2fs"
LIB_FLAGS="$BUILD/lib/ext2fs/libext2fs.a $BUILD/lib/et/libcom_err.a"
```
Drop libtool-only install targets (`install-libLTLIBRARIES`) and link the archives straight out of the build tree.

### 4. Transitive link deps with no MinGW package on the host
Symptom: `-lz` unresolved; the distro has `mingw-w64-gcc` but no `mingw-w64-zlib`.

Fix: build the dep from source into a prefix and pass it through an env var:
```sh
make -C zlib -f win32/Makefile.gcc PREFIX=x86_64-w64-mingw32- libz.a
mkdir -p $P/include $P/lib && cp zlib.h zconf.h $P/include/ && cp libz.a $P/lib/
# script: ZLIB_FLAGS="-I$ZLIB_PREFIX/include -L$ZLIB_PREFIX/lib"
```

## Configure is slow, not hung

Every MinGW probe compiles; a large configure can take 10–30 min and looks stalled mid-check (`checking size of off_t...`). Do not raise timeouts blindly — pass `--cache-file="$BUILD/config.cache"` so the first run pays once and reruns are seconds. Only re-run configure when `$BUILD/Makefile` is absent.

## Make it reproducible

- Pin sources in CI by tag/depth-1 clone (`--branch v1.47.3`), never "latest".
- Document every required env var in the tool's README — an undocumented `SRC`/`PREFIX` var means the documented build cannot produce the binary.
- Verify the artifact, don't trust exit status: `file out.exe` → `PE32+ executable ... x86-64`.

## Cross-platform behaviour caveats

State the boundary in the tool's own contract instead of degrading silently. `canoe-ext4` refuses mutation on a dirty source with exit 4 and requires explicit `--recover`; the Windows build links the same replay objects (`debugfs/journal.c`, `e2fsck/revoke.c`, `e2fsck/recovery.c`) over the sector-aware Windows I/O manager, so dirty-source recovery is available there too, while discard/zeroout stay out of the replay path. Every other unsupported case fails closed with a distinct exit code rather than attempting a partial operation.
