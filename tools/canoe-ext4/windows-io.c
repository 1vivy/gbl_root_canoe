/* Sector-aware Win32 transport for libext2fs. No filesystem policy lives here.
 * Raw Windows disks require sector-aligned offsets and lengths even with
 * buffered handles. Partial-sector writes preserve the untouched bytes.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include "windows-io.h"
#include <winioctl.h>

typedef struct {
    HANDLE handle;
    DWORD sector;
    int writable;
    struct struct_io_stats stats;
} windows_disk;

static errcode_t error(const char *operation) {
    DWORD code = GetLastError();
    fprintf(stderr, "canoe-ext4: Windows %s failed (error %lu)\n", operation, code);
    return EIO;
}

static errcode_t geometry(HANDLE handle, DWORD *sector) {
    DISK_GEOMETRY disk;
    DWORD returned;
    if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                        &disk, sizeof(disk), &returned, NULL)) {
        *sector = disk.BytesPerSector;
        if (*sector < 512 || *sector > 65536 || (*sector & (*sector - 1)))
            return EINVAL;
        return 0;
    }
    /* Ordinary files need no sector expansion. A raw disk has no file size. */
    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size)) return error("query disk geometry");
    *sector = 1;
    return 0;
}

static errcode_t exact(HANDLE handle, uint64_t offset, void *buf, DWORD bytes, int write) {
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(handle, position, NULL, FILE_BEGIN)) return error("seek");
    DWORD done = 0;
    BOOL ok = write ? WriteFile(handle, buf, bytes, &done, NULL)
                    : ReadFile(handle, buf, bytes, &done, NULL);
    if (!ok) return error(write ? "write" : "read");
    if (done != bytes) return write ? EXT2_ET_SHORT_WRITE : EXT2_ET_SHORT_READ;
    return 0;
}

static errcode_t transfer(windows_disk *disk, uint64_t offset, size_t bytes, void *buffer, int write) {
    if (write && !disk->writable) return EACCES;
    if (offset > INT64_MAX || bytes > (uint64_t)INT64_MAX - offset) return EOVERFLOW;
    if (!bytes) return 0;
    DWORD sector = disk->sector;
    unsigned char *buf = buffer;
    unsigned char *bounce = VirtualAlloc(NULL, sector, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!bounce) return ENOMEM;
    errcode_t rc = 0;
    while (bytes) {
        DWORD within = (DWORD)(offset % sector);
        if (within || bytes < sector) {
            DWORD take = sector - within;
            if (take > bytes) take = (DWORD)bytes;
            rc = exact(disk->handle, offset - within, bounce, sector, 0);
            if (rc) break;
            if (write) {
                memcpy(bounce + within, buf, take);
                rc = exact(disk->handle, offset - within, bounce, sector, 1);
            } else memcpy(buf, bounce + within, take);
            if (rc) break;
            offset += take; buf += take; bytes -= take;
        } else {
            DWORD take = (DWORD)(bytes > 1048576 ? 1048576 : bytes);
            take -= take % sector;
            rc = exact(disk->handle, offset, buf, take, write);
            if (rc) break;
            offset += take; buf += take; bytes -= take;
        }
    }
    VirtualFree(bounce, 0, MEM_RELEASE);
    return rc;
}

int canoe_windows_pread(HANDLE handle, void *buffer, size_t count, unsigned long long offset) {
    windows_disk disk = { .handle = handle };
    if (count > INT_MAX || geometry(handle, &disk.sector) || transfer(&disk, offset, count, buffer, 0)) {
        errno = EIO;
        return -1;
    }
    return (int)count;
}

static errcode_t open_disk(const char *name, int flags, io_channel *result) {
    io_channel channel = calloc(1, sizeof(*channel));
    windows_disk *disk = calloc(1, sizeof(*disk));
    if (!channel || !disk) { free(channel); free(disk); return ENOMEM; }
    disk->writable = !!(flags & IO_FLAG_RW);
    disk->handle = CreateFileA(name, GENERIC_READ | (disk->writable ? GENERIC_WRITE : 0),
        (flags & IO_FLAG_EXCLUSIVE) ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    errcode_t rc = 0;
    if (disk->handle == INVALID_HANDLE_VALUE) rc = error("open");
    else rc = geometry(disk->handle, &disk->sector);
    if (!rc && !(channel->name = strdup(name))) rc = ENOMEM;
    if (rc) {
        if (disk->handle != INVALID_HANDLE_VALUE) CloseHandle(disk->handle);
        free(disk); free(channel); return rc;
    }
    channel->magic = EXT2_ET_MAGIC_IO_CHANNEL;
    channel->manager = canoe_windows_io_manager;
    channel->block_size = 1024;
    channel->refcount = 1;
    channel->private_data = disk;
    if (disk->sector > 1) channel->flags = CHANNEL_FLAGS_BLOCK_DEVICE;
    disk->stats.num_fields = 2;
    *result = channel;
    return 0;
}
static errcode_t flush_disk(io_channel channel) {
    windows_disk *disk = channel->private_data;
    if (disk->writable && !FlushFileBuffers(disk->handle)) return error("flush");
    return 0;
}
static errcode_t close_disk(io_channel channel) {
    if (--channel->refcount > 0) return 0;
    windows_disk *disk = channel->private_data;
    errcode_t rc = flush_disk(channel);
    if (!CloseHandle(disk->handle) && !rc) rc = error("close");
    free(disk); free(channel->name); free(channel);
    return rc;
}
static errcode_t block_size(io_channel channel, int size) {
    if (size <= 0) return EINVAL;
    channel->block_size = size; return 0;
}
static errcode_t blocks(io_channel channel, uint64_t block, int count, void *buf, int write) {
    windows_disk *disk = channel->private_data;
    if (block > INT64_MAX / (uint64_t)channel->block_size) return EOVERFLOW;
    uint64_t size = count < 0 ? (uint64_t)(-(int64_t)count) : (uint64_t)count * channel->block_size;
    if (size > SIZE_MAX) return EOVERFLOW;
    errcode_t rc = transfer(disk, block * channel->block_size, (size_t)size, buf, write);
    if (!rc) { if (write) disk->stats.bytes_written += size; else disk->stats.bytes_read += size; }
    return rc;
}
static errcode_t read64(io_channel c, unsigned long long b, int n, void *p) { return blocks(c,b,n,p,0); }
static errcode_t write64(io_channel c, unsigned long long b, int n, const void *p) { return blocks(c,b,n,(void *)p,1); }
static errcode_t read32(io_channel c, unsigned long b, int n, void *p) { return read64(c,b,n,p); }
static errcode_t write32(io_channel c, unsigned long b, int n, const void *p) { return write64(c,b,n,p); }
static errcode_t write_byte(io_channel c, unsigned long offset, int count, const void *p) {
    if (count < 0) return EINVAL;
    return transfer(c->private_data, offset, (size_t)count, (void *)p, 1);
}
static errcode_t stats(io_channel c, io_stats *out) { *out = &((windows_disk *)c->private_data)->stats; return 0; }
static struct struct_io_manager manager = {
    .magic=EXT2_ET_MAGIC_IO_MANAGER, .name="Canoe Win32 sector I/O", .open=open_disk,
    .close=close_disk, .set_blksize=block_size, .read_blk=read32, .write_blk=write32,
    .flush=flush_disk, .write_byte=write_byte, .get_stats=stats, .read_blk64=read64, .write_blk64=write64,
};
io_manager canoe_windows_io_manager = &manager;
