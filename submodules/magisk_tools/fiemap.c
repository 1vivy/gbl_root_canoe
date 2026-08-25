#define _FILE_OFFSET_BITS 64


#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

struct run {
    uint64_t physical;
    uint64_t blocks;
};

static int fail_message(const char *message)
{
    fprintf(stderr, "fiemap: %s\n", message);
    return 1;
}

static int fail_errno(const char *operation)
{
    fprintf(stderr, "fiemap: %s: %s\n", operation, strerror(errno));
    return 1;
}

static int append_run(struct run **runs, size_t *count, size_t *capacity,
                      uint64_t physical, uint64_t blocks)
{
    if (*count > 0 && (*runs)[*count - 1].physical + (*runs)[*count - 1].blocks == physical) {
        (*runs)[*count - 1].blocks += blocks;
        return 0;
    }
    if (*count == *capacity) {
        size_t next_capacity = *capacity == 0 ? 16 : *capacity * 2;
        struct run *next = realloc(*runs, next_capacity * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *runs = next;
        *capacity = next_capacity;
    }
    (*runs)[*count].physical = physical;
    (*runs)[*count].blocks = blocks;
    *count += 1;
    return 0;
}

static int check_extent_flags(uint32_t flags)
{
    if ((flags & FIEMAP_EXTENT_UNWRITTEN) != 0) {
        return fail_message("unwritten or preallocated extent");
    }
    if ((flags & FIEMAP_EXTENT_DATA_INLINE) != 0) {
        return fail_message("inline data");
    }
    if ((flags & FIEMAP_EXTENT_DATA_ENCRYPTED) != 0) {
        return fail_message("encrypted extent");
    }
    return 0;
}

static int collect_extents(int fd, uint32_t block_size, uint64_t file_size,
                           struct run **runs, size_t *run_count)
{
    const size_t capacity = 256;
    const size_t allocation_size = sizeof(struct fiemap) +
                                   capacity * sizeof(struct fiemap_extent);
    struct fiemap *mapping = calloc(1, allocation_size);
    size_t run_capacity = 0;
    uint64_t logical = 0;
    uint64_t needed = file_size / block_size;
    uint64_t covered = 0;

    if (file_size % block_size != 0) {
        needed += 1;
    }
    if (mapping == NULL) {
        return fail_message("out of memory");
    }
    for (;;) {
        uint32_t index;
        uint64_t next_logical = logical;
        int last_extent = 0;

        memset(mapping, 0, allocation_size);
        mapping->fm_start = logical;
        mapping->fm_length = UINT64_MAX;
        mapping->fm_extent_count = capacity;
        if (ioctl(fd, FS_IOC_FIEMAP, mapping) < 0) {
            free(mapping);
            return fail_errno("FS_IOC_FIEMAP");
        }
        if (mapping->fm_mapped_extents == 0) {
            free(mapping);
            return logical < file_size ? fail_message("holes detected") : 0;
        }
        for (index = 0; index < mapping->fm_mapped_extents; ++index) {
            const struct fiemap_extent *extent = &mapping->fm_extents[index];
            uint64_t blocks;

            if (check_extent_flags(extent->fe_flags) != 0) {
                free(mapping);
                free(*runs);
                *runs = NULL;
                *run_count = 0;
                return 1;
            }
            if (extent->fe_logical != next_logical || extent->fe_length == 0) {
                free(mapping);
                free(*runs);
                *runs = NULL;
                *run_count = 0;
                return fail_message("holes detected");
            }
            if ((extent->fe_logical % block_size) != 0 ||
                (extent->fe_physical % block_size) != 0 ||
                (extent->fe_length % block_size) != 0) {
                free(mapping);
                free(*runs);
                *runs = NULL;
                *run_count = 0;
                return fail_message("unaligned extent");
            }
            blocks = extent->fe_length / block_size;
            if (append_run(runs, run_count, &run_capacity,
                           extent->fe_physical / block_size, blocks) != 0) {
                free(mapping);
                free(*runs);
                *runs = NULL;
                *run_count = 0;
                return fail_message("out of memory");
            }
            if (UINT64_MAX - next_logical < extent->fe_length ||
                UINT64_MAX - covered < blocks) {
                free(mapping);
                free(*runs);
                *runs = NULL;
                *run_count = 0;
                return fail_message("extent overflow");
            }
            next_logical += extent->fe_length;
            covered += blocks;
            last_extent = (extent->fe_flags & FIEMAP_EXTENT_LAST) != 0;
        }
        if (last_extent) {
            break;
        }
        if (next_logical <= logical) {
            free(mapping);
            free(*runs);
            *runs = NULL;
            *run_count = 0;
            return fail_message("FIEMAP did not advance");
        }
        logical = next_logical;
    }
    free(mapping);
    if (covered < needed) {
        free(*runs);
        *runs = NULL;
        *run_count = 0;
        return fail_message("holes detected");
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct stat st;
    struct statvfs vfs;
    struct run *runs = NULL;
    size_t run_count = 0;
    uint32_t block_size;
    int fd;
    size_t index;

    if (argc != 2) {
        fprintf(stderr, "usage: fiemap <path>\n");
        return 2;
    }
    fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return fail_errno("open");
    }
    if (fstat(fd, &st) < 0) {
        const int result = fail_errno("fstat");
        close(fd);
        return result;
    }
    if (fstatvfs(fd, &vfs) < 0 || vfs.f_bsize == 0 || vfs.f_bsize > UINT32_MAX) {
        const int result = fail_errno("fstatvfs");
        close(fd);
        return result;
    }
    block_size = (uint32_t)vfs.f_bsize;
    printf("blocksize:%" PRIu32 "\n", block_size);
    if (collect_extents(fd, block_size, (uint64_t)st.st_size, &runs, &run_count) != 0) {
        close(fd);
        return 1;
    }
    if (close(fd) < 0) {
        free(runs);
        return fail_errno("close");
    }
    for (index = 0; index < run_count; ++index) {
        printf("%" PRIu64 ":%" PRIu64 "\n", runs[index].physical, runs[index].blocks);
    }
    free(runs);
    return 0;
}
