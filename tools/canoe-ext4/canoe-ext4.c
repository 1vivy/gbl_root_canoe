#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <setjmp.h>

#ifndef _WIN32
#include <sys/file.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <io.h>
#include <process.h>
#include <windows.h>
#define close _close
#define fsync _commit
#define getpid _getpid
#define O_RDONLY _O_RDONLY
#define O_RDWR _O_RDWR
#define O_BINARY _O_BINARY
#endif

#include <et/com_err.h>
#include <ext2fs/ext2_err.h>
#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>
#include <ext2fs/ext3_extents.h>

#ifdef _WIN32
#include "windows-io.h"
#undef default_io_manager
#define default_io_manager canoe_windows_io_manager
#endif

#define EXIT_OK 0
#define EXIT_USAGE 2
#define EXIT_UNSUPPORTED 3
#define EXIT_DIRTY 4
#define EXIT_MOUNTED 5
#define EXIT_IO 6
#define EXIT_NOT_FOUND 7
#define EXIT_OPERATION 8

#define MAX_PATH_BYTES 4096U
#define MAX_COMPONENT_BYTES 255U
#define MAX_WRITE_BYTES (64U * 1024U * 1024U)
#define IO_CHUNK_BYTES (64U * 1024U)
#define MAX_TRANSACTION_ENTRIES 4096U
#define MAX_SYNC_MANIFEST_BYTES \
    (MAX_TRANSACTION_ENTRIES * (4U * (MAX_PATH_BYTES - 1U) + 6U))
#define MAX_SYNC_MANIFEST_LINE_BYTES (4U * (MAX_PATH_BYTES - 1U) + 6U)
#define MAX_TRANSACTION_BYTES (256U * 1024U * 1024U)

static int source_fd = -1;
static ext2_filsys fs;
static bool mutating;
static const char *source_name;
enum transaction_phase {
    TRANSACTION_NONE,
    TRANSACTION_APPLY,
    CANOE_TRANSACTION_ROLLBACK,
};

static jmp_buf transaction_jump;
static enum transaction_phase transaction_phase;
static int transaction_error;

typedef struct {
    bool recover;
    bool mkdir_p;
    const char *inspect_path;
    int command_index;
} options_t;

static void print_rc(const char *operation, errcode_t rc) {
    fprintf(stderr, "%s: %s (rc=%lu)\n", operation, error_message(rc),
            (unsigned long)rc);
}

static void close_quietly(void) {
    if (fs != NULL) {
        (void)ext2fs_close2(fs, 0);
        fs = NULL;
    }
    if (source_fd >= 0) {
        close(source_fd);
        source_fd = -1;
    }
}

static void transaction_fail(int code) {
    if (transaction_phase == TRANSACTION_NONE) {
        close_quietly();
        exit(code);
    }
    if (fs != NULL) {
        (void)ext2fs_close2(fs, 0);
        fs = NULL;
    }
    transaction_error = code;
    longjmp(transaction_jump, 1);
}

static void fail(int code, const char *message) {
    fprintf(stderr, "canoe-ext4: %s\n", message);
    transaction_fail(code);
}

static void fail_rc(int code, const char *operation, errcode_t rc) {
    print_rc(operation, rc);
    transaction_fail(code);
}

static int host_pread(int fd, void *buf, size_t count, off_t offset) {
#ifdef _WIN32
    return canoe_windows_pread((HANDLE)_get_osfhandle(fd), buf, count, (unsigned long long)offset);
#else
    ssize_t got = pread(fd, buf, count, offset);
    if (got > INT_MAX)
        return -1;
    return (int)got;
#endif
}

static int lock_source(int fd) {
#ifdef _WIN32
    (void)fd;
    return 0;
#else
    /* udev briefly holds a shared lock while probing newly exposed media,
     * including between our mkdir and write helper invocations. Keep exclusive
     * ownership, but give that probe a bounded opportunity to finish. */
    struct timespec started, now;
    if (clock_gettime(CLOCK_MONOTONIC, &started) < 0)
        return -1;
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0)
            return 0;
        int lock_error = errno;
        if (lock_error != EWOULDBLOCK && lock_error != EINTR)
            return -1;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
            return -1;
        int64_t elapsed = (int64_t)(now.tv_sec - started.tv_sec) * 1000000000
                          + now.tv_nsec - started.tv_nsec;
        if (elapsed >= 2000000000) {
            errno = EWOULDBLOCK;
            return -1;
        }
        struct timespec pause = { .tv_sec = 0, .tv_nsec = 50000000 };
        nanosleep(&pause, NULL);
    }
#endif
}

static bool source_is_mounted(const char *path, const struct stat *source_stat) {
#ifdef _WIN32
    (void)path;
    (void)source_stat;
    return false;
#else
    const char *mountinfo = getenv("CANOE_EXT4_MOUNTINFO");
    if (mountinfo == NULL || *mountinfo == '\0')
        mountinfo = "/proc/self/mountinfo";
    FILE *stream = fopen(mountinfo, "r");
    if (stream == NULL)
        return false;

    char canonical[PATH_MAX];
    const char *wanted = realpath(path, canonical) != NULL ? canonical : path;
    char *line = NULL;
    size_t capacity = 0;
    bool mounted = false;
    while (!mounted && getline(&line, &capacity, stream) >= 0) {
        char *separator = strstr(line, " - ");
        if (separator == NULL)
            continue;
        *separator = '\0';
        char *save = NULL;
        char *field = strtok_r(line, " ", &save);
        unsigned int field_number = 0;
        char *device = NULL;
        while (field != NULL) {
            ++field_number;
            if (field_number == 3)
                device = field;
            field = strtok_r(NULL, " ", &save);
        }
        char *post_save = NULL;
        char *fstype = strtok_r(separator + 3, " ", &post_save);
        char *mount_source = strtok_r(NULL, " ", &post_save);
        (void)fstype;
        if (mount_source == NULL)
            continue;

        char decoded[PATH_MAX];
        size_t in = 0;
        size_t out = 0;
        while (mount_source[in] != '\0' && out + 1 < sizeof(decoded)) {
            if (mount_source[in] == '\\' && mount_source[in + 1] != '\0') {
                unsigned int value = 0;
                int digits = 0;
                for (size_t n = 1; n <= 3; ++n) {
                    char c = mount_source[in + n];
                    if (c < '0' || c > '7')
                        break;
                    value = (value * 8U) + (unsigned int)(c - '0');
                    ++digits;
                }
                if (digits == 3) {
                    decoded[out++] = (char)value;
                    in += 4;
                    continue;
                }
            }
            decoded[out++] = mount_source[in++];
        }
        decoded[out] = '\0';
        if (strcmp(decoded, wanted) == 0 || strcmp(decoded, path) == 0) {
            mounted = true;
            break;
        }
        if (S_ISBLK(source_stat->st_mode) && device != NULL) {
            unsigned int major_number = 0;
            unsigned int minor_number = 0;
            if (sscanf(device, "%u:%u", &major_number, &minor_number) == 2 &&
                major(source_stat->st_rdev) == major_number &&
                minor(source_stat->st_rdev) == minor_number) {
                mounted = true;
            }
        }
    }
    free(line);
    fclose(stream);
    return mounted;
#endif
}

static void normalize_path(const char *input, char output[MAX_PATH_BYTES]) {
    size_t length = strlen(input);
    if (length == 0 || length >= MAX_PATH_BYTES || input[0] != '/')
        fail(EXIT_USAGE, "paths must be absolute and shorter than 4096 bytes");
    size_t out = 0;
    output[out++] = '/';
    size_t component_start = 1;
    for (size_t i = 1; i <= length; ++i) {
        if (input[i] != '/' && input[i] != '\0')
            continue;
        size_t component_length = i - component_start;
        if (component_length == 0) {
            component_start = i + 1;
            continue;
        }
        if (component_length > MAX_COMPONENT_BYTES ||
            (component_length == 1 && input[component_start] == '.') ||
            (component_length == 2 && input[component_start] == '.' &&
             input[component_start + 1] == '.'))
            fail(EXIT_USAGE, "path has an invalid component");
        if (out > 1)
            output[out++] = '/';
        memcpy(output + out, input + component_start, component_length);
        out += component_length;
        component_start = i + 1;
    }
    if (out > 1 && output[out - 1] == '/')
        --out;
    output[out] = '\0';
}

static ext2_ino_t lookup_path(const char *path, bool missing_ok) {
    ext2_ino_t inode = 0;
    errcode_t rc = ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, &inode);
    if (rc != 0) {
        if (missing_ok && rc == EXT2_ET_FILE_NOT_FOUND)
            return 0;
        fail_rc(rc == EXT2_ET_FILE_NOT_FOUND ? EXIT_NOT_FOUND : EXIT_IO, "lookup", rc);
    }
    return inode;
}

static void split_parent(const char *path, char parent[MAX_PATH_BYTES],
                         char name[MAX_COMPONENT_BYTES + 1]) {
    char copy[MAX_PATH_BYTES];
    size_t length = strlen(path);
    if (length <= 1 || length >= sizeof(copy))
        fail(EXIT_USAGE, "operation requires a non-root path");
    memcpy(copy, path, length + 1);
    char *slash = strrchr(copy, '/');
    if (slash == NULL || slash[1] == '\0')
        fail(EXIT_USAGE, "path has no final component");
    if (strlen(slash + 1) > MAX_COMPONENT_BYTES)
        fail(EXIT_USAGE, "path component is too long");
    memcpy(name, slash + 1, strlen(slash + 1) + 1);
    if (slash == copy)
        strcpy(parent, "/");
    else {
        *slash = '\0';
        strcpy(parent, copy);
    }
}

static void read_superblock(struct ext2_super_block *super) {
    unsigned char block[1024];
    int got = host_pread(source_fd, block, sizeof(block), 1024);
    if (got != (int)sizeof(block))
        fail(EXIT_IO, "cannot read the ext4 superblock");
    memcpy(super, block, sizeof(*super));
    if (super->s_magic != EXT2_SUPER_MAGIC)
        fail(EXIT_OPERATION, "source is not an ext2/ext4 filesystem");
}

static uint32_t unknown_incompat(const struct ext2_super_block *super) {
    return super->s_feature_incompat & ~((uint32_t)EXT2_LIB_FEATURE_INCOMPAT_SUPP);
}

static uint32_t unknown_ro_compat(const struct ext2_super_block *super) {
    return super->s_feature_ro_compat & ~((uint32_t)EXT2_LIB_FEATURE_RO_COMPAT_SUPP);
}

static bool is_dirty(const struct ext2_super_block *super) {
    return (super->s_state & EXT2_VALID_FS) == 0 ||
           (super->s_state & EXT2_ERROR_FS) != 0;
}

static const char *feature_name_compat(uint32_t bit) {
    switch (bit) {
    case EXT2_FEATURE_COMPAT_DIR_PREALLOC: return "dir_prealloc";
    case EXT2_FEATURE_COMPAT_IMAGIC_INODES: return "imagic_inodes";
    case EXT3_FEATURE_COMPAT_HAS_JOURNAL: return "has_journal";
    case EXT2_FEATURE_COMPAT_EXT_ATTR: return "ext_attr";
    case EXT2_FEATURE_COMPAT_RESIZE_INODE: return "resize_inode";
    case EXT2_FEATURE_COMPAT_DIR_INDEX: return "dir_index";
    case EXT2_FEATURE_COMPAT_LAZY_BG: return "lazy_bg";
    case EXT2_FEATURE_COMPAT_EXCLUDE_BITMAP: return "exclude_bitmap";
    case EXT4_FEATURE_COMPAT_SPARSE_SUPER2: return "sparse_super2";
    case EXT4_FEATURE_COMPAT_FAST_COMMIT: return "fast_commit";
    case EXT4_FEATURE_COMPAT_STABLE_INODES: return "stable_inodes";
    case EXT4_FEATURE_COMPAT_ORPHAN_FILE: return "orphan_file";
    default: return NULL;
    }
}

static const char *feature_name_ro(uint32_t bit) {
    switch (bit) {
    case EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER: return "sparse_super";
    case EXT2_FEATURE_RO_COMPAT_LARGE_FILE: return "large_file";
    case EXT4_FEATURE_RO_COMPAT_HUGE_FILE: return "huge_file";
    case EXT4_FEATURE_RO_COMPAT_GDT_CSUM: return "gdt_csum";
    case EXT4_FEATURE_RO_COMPAT_DIR_NLINK: return "dir_nlink";
    case EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE: return "extra_isize";
    case EXT4_FEATURE_RO_COMPAT_HAS_SNAPSHOT: return "has_snapshot";
    case EXT4_FEATURE_RO_COMPAT_QUOTA: return "quota";
    case EXT4_FEATURE_RO_COMPAT_BIGALLOC: return "bigalloc";
    case EXT4_FEATURE_RO_COMPAT_METADATA_CSUM: return "metadata_csum";
    case EXT4_FEATURE_RO_COMPAT_REPLICA: return "replica";
    case EXT4_FEATURE_RO_COMPAT_READONLY: return "readonly";
    case EXT4_FEATURE_RO_COMPAT_PROJECT: return "project";
    case EXT4_FEATURE_RO_COMPAT_SHARED_BLOCKS: return "shared_blocks";
    case EXT4_FEATURE_RO_COMPAT_VERITY: return "verity";
    case EXT4_FEATURE_RO_COMPAT_ORPHAN_PRESENT: return "orphan_present";
    default: return NULL;
    }
}

static const char *feature_name_incompat(uint32_t bit) {
    switch (bit) {
    case EXT2_FEATURE_INCOMPAT_COMPRESSION: return "compression";
    case EXT2_FEATURE_INCOMPAT_FILETYPE: return "filetype";
    case EXT3_FEATURE_INCOMPAT_RECOVER: return "journal_needs_recovery";
    case EXT3_FEATURE_INCOMPAT_JOURNAL_DEV: return "journal_dev";
    case EXT2_FEATURE_INCOMPAT_META_BG: return "meta_bg";
    case EXT3_FEATURE_INCOMPAT_EXTENTS: return "extents";
    case EXT4_FEATURE_INCOMPAT_64BIT: return "64bit";
    case EXT4_FEATURE_INCOMPAT_MMP: return "mmp";
    case EXT4_FEATURE_INCOMPAT_FLEX_BG: return "flex_bg";
    case EXT4_FEATURE_INCOMPAT_EA_INODE: return "ea_inode";
    case EXT4_FEATURE_INCOMPAT_DIRDATA: return "dirdata";
    case EXT4_FEATURE_INCOMPAT_CSUM_SEED: return "csum_seed";
    case EXT4_FEATURE_INCOMPAT_LARGEDIR: return "largedir";
    case EXT4_FEATURE_INCOMPAT_INLINE_DATA: return "inline_data";
    case EXT4_FEATURE_INCOMPAT_ENCRYPT: return "encrypt";
    case EXT4_FEATURE_INCOMPAT_CASEFOLD: return "casefold";
    default: return NULL;
    }
}

static void print_feature_list(uint32_t flags, const char *(*name)(uint32_t)) {
    bool first = true;
    putchar('[');
    for (unsigned int bit = 1; bit != 0; bit <<= 1) {
        if ((flags & bit) == 0)
            continue;
        const char *feature = name(bit);
        if (!first)
            putchar(',');
        first = false;
        if (feature == NULL)
            printf("\"unknown-0x%08x\"", bit);
        else
            printf("\"%s\"", feature);
    }
    puts("]");
}

static void print_json_string(const char *value) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        switch (*p) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (*p < 0x20)
                printf("\\u%04x", *p);
            else
                putchar(*p);
            break;
        }
    }
    putchar('"');
}

static void check_supported(const struct ext2_super_block *super) {
    uint32_t incompat = unknown_incompat(super);
    uint32_t ro_compat = unknown_ro_compat(super);
    if (incompat != 0 || ro_compat != 0)
        fail(EXIT_UNSUPPORTED, "filesystem has unsupported feature bits");
}

#ifndef _WIN32
static void recover_journal(const char *path, bool force) {
    int output_pipe[2];
    if (pipe(output_pipe) < 0)
        fail(EXIT_IO, "cannot create journal recovery pipe");
    /*
     * The recovery child inherits source_fd and its flock open-file
     * description.  The parent waits below, so recovery cannot retain that
     * lock beyond this helper invocation.
     */
    pid_t child = fork();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        fail(EXIT_IO, "cannot start journal recovery");
    }
    if (child == 0) {
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe[1], STDERR_FILENO) < 0)
            _exit(127);
        close(output_pipe[1]);
        if (force)
            execlp("e2fsck", "e2fsck", "-fy", path, (char *)NULL);
        else
            execlp("e2fsck", "e2fsck", "-p", path, (char *)NULL);
        _exit(127);
    }
    close(output_pipe[1]);
    char output[8192];
    char chunk[4096];
    size_t used = 0;
    for (;;) {
        ssize_t got = read(output_pipe[0], chunk, sizeof(chunk));
        if (got <= 0)
            break;
        if (used + 1 < sizeof(output)) {
            size_t copy = (size_t)got;
            if (copy > sizeof(output) - used - 1)
                copy = sizeof(output) - used - 1;
            memcpy(output + used, chunk, copy);
            used += copy;
        }
    }
    close(output_pipe[0]);
    int status = 0;
    if (waitpid(child, &status, 0) < 0)
        fail(EXIT_IO, "cannot wait for journal recovery");
    if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 1))
        fail(EXIT_IO, "journal recovery failed");
    output[used] = '\0';
    fprintf(stderr, "journal_recovery=completed\n");
    if (force && strstr(output, "recovering journal") != NULL)
        fprintf(stderr, "journal_replay=performed\n");
}
#else
/*
 * e2fsck is not available in the Windows toolkit.  e2fsprogs nevertheless
 * ships the same jbd2 replay implementation used by e2fsck/debugfs; the
 * Windows build links those objects and uses this entry point directly.
 */
extern errcode_t ext2fs_run_ext3_journal(ext2_filsys *fs);

static void recover_journal(const char *path, bool force, bool dirty) {
    if (!force)
        return;

    ext2_filsys recovery_fs = NULL;
    errcode_t rc = ext2fs_open(path, EXT2_FLAG_64BITS | EXT2_FLAG_RW, 0, 0,
                               default_io_manager, &recovery_fs);
    if (rc != 0)
        fail_rc(EXIT_IO, "recovery-open", rc);
    bool journal_was_pending =
        ext2fs_has_feature_journal_needs_recovery(recovery_fs->super);

    rc = ext2fs_run_ext3_journal(&recovery_fs);
    if (rc != 0) {
        if (recovery_fs != NULL)
            (void)ext2fs_close2(recovery_fs, 0);
        fail_rc(EXIT_IO, "journal-recovery", rc);
    }
    /*
     * ext2fs_run_ext3_journal deliberately leaves the EXT2_VALID_FS bit
     * unset: e2fsck normally decides that after its consistency checks.
     * Replay itself is the only recovery operation available here, so only
     * a successful pending-journal replay may close that dirty-state gate.
     */
    if (dirty && journal_was_pending &&
        (recovery_fs->super->s_state & EXT2_ERROR_FS) == 0) {
        ext2fs_mark_valid(recovery_fs);
        recovery_fs->super->s_state |= EXT2_VALID_FS;
        ext2fs_mark_super_dirty(recovery_fs);
        rc = ext2fs_flush(recovery_fs);
        if (rc != 0) {
            (void)ext2fs_close2(recovery_fs, 0);
            fail_rc(EXIT_IO, "recovery-flush", rc);
        }
    }
    if (recovery_fs != NULL) {
        rc = ext2fs_close2(recovery_fs, 0);
        recovery_fs = NULL;
        if (rc != 0)
            fail_rc(EXIT_IO, "recovery-close", rc);
    }
    fprintf(stderr, "journal_recovery=completed\n");
    if (journal_was_pending)
        fprintf(stderr, "journal_replay=performed\n");
}
#endif

static void open_filesystem(bool writable, bool recover) {
    struct ext2_super_block raw_super;
    read_superblock(&raw_super);
    check_supported(&raw_super);
    if (writable && (raw_super.s_feature_ro_compat & EXT4_FEATURE_RO_COMPAT_READONLY) != 0)
        fail(EXIT_UNSUPPORTED, "filesystem is marked read-only by its feature flags");
    bool dirty = is_dirty(&raw_super);
    if (writable && dirty && !recover)
        fail(EXIT_DIRTY, "filesystem is dirty; retry with --recover");
#ifndef _WIN32
    if (writable)
        recover_journal(source_name, recover || dirty);
#else
    if (writable)
        recover_journal(source_name, recover || dirty, dirty);
#endif

    int flags = EXT2_FLAG_64BITS | (writable ? EXT2_FLAG_RW : 0);
    errcode_t rc = ext2fs_open(source_name, flags, 0, 0, default_io_manager, &fs);
    if (rc != 0)
        fail_rc(EXIT_UNSUPPORTED, "open", rc);
    if (writable) {
        rc = ext2fs_read_bitmaps(fs);
        if (rc != 0)
            fail_rc(EXIT_IO, "read-bitmaps", rc);
    }
    struct ext2_super_block after_recovery = *fs->super;
    if (writable && is_dirty(&after_recovery))
        fail(EXIT_DIRTY, "journal recovery did not leave a clean filesystem");
}


static int finish_success(void) {
    errcode_t rc;
    if (fs != NULL) {
        if (mutating) {
            rc = ext2fs_flush(fs);
            if (rc != 0) {
                print_rc("flush", rc);
                close_quietly();
                return EXIT_IO;
            }
        }
        /* Direct libext2fs file operations do not hold a jbd2 handle. Closing
         * the filesystem is therefore the journal-stop boundary. */
        rc = ext2fs_close(fs);
        fs = NULL;
        if (rc != 0) {
            print_rc("close", rc);
            close_quietly();
            return EXIT_IO;
        }
    }
    if (source_fd >= 0) {
        if (mutating && fsync(source_fd) < 0) {
            fprintf(stderr, "canoe-ext4: fsync: %s\n", strerror(errno));
            close_quietly();
            return EXIT_IO;
        }
        close(source_fd);
        source_fd = -1;
    }
    return EXIT_OK;
}

static void finish_sync_transaction(void) {
    errcode_t rc = ext2fs_flush(fs);
    if (rc != 0)
        fail_rc(EXIT_IO, "sync-flush", rc);
    rc = ext2fs_close(fs);
    if (rc != 0)
        fail_rc(EXIT_IO, "sync-close", rc);
    fs = NULL;
    if (fsync(source_fd) < 0) {
        int sync_error = errno;
        fprintf(stderr, "canoe-ext4: sync-fsync: %s\n", strerror(sync_error));
        transaction_fail(EXIT_IO);
    }
    close(source_fd);
    source_fd = -1;
}
static void read_file_to_stdout(const char *path) {
    ext2_ino_t inode = lookup_path(path, false);
    struct ext2_inode metadata;
    errcode_t rc = ext2fs_read_inode(fs, inode, &metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-inode", rc);
    if (!LINUX_S_ISREG(metadata.i_mode))
        fail(EXIT_OPERATION, "path is not a regular file");
    ext2_file_t file;
    rc = ext2fs_file_open(fs, inode, 0, &file);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-open", rc);
    unsigned char buffer[IO_CHUNK_BYTES];
    __u64 remaining = 0;
    rc = ext2fs_file_get_lsize(file, &remaining);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-size", rc);
    while (remaining > 0) {
        unsigned int wanted = remaining > sizeof(buffer) ? sizeof(buffer) : (unsigned int)remaining;
        unsigned int got = 0;
        rc = ext2fs_file_read(file, buffer, wanted, &got);
        if (rc != 0 || got == 0) {
            (void)ext2fs_file_close(file);
            if (rc != 0)
                fail_rc(EXIT_IO, "read", rc);
            fail(EXIT_IO, "short read");
        }
        size_t written = fwrite(buffer, 1, got, stdout);
        if (written != got) {
            (void)ext2fs_file_close(file);
            fail(EXIT_IO, "stdout write failed");
        }
        remaining -= got;
    }
    rc = ext2fs_file_close(file);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-close", rc);
}

static void read_stdin(unsigned char **data, size_t *length) {
    size_t used = 0;
    size_t capacity = 0;
    unsigned char *buffer = NULL;
    for (;;) {
        if (used == capacity) {
            size_t next = capacity == 0 ? IO_CHUNK_BYTES : capacity * 2;
            if (next > MAX_WRITE_BYTES)
                next = MAX_WRITE_BYTES;
            if (next == capacity)
                fail(EXIT_OPERATION, "stdin exceeds the 64 MiB write limit");
            unsigned char *grown = realloc(buffer, next);
            if (grown == NULL) {
                free(buffer);
                fail(EXIT_IO, "out of memory while buffering stdin");
            }
            buffer = grown;
            capacity = next;
        }
        size_t got = fread(buffer + used, 1, capacity - used, stdin);
        used += got;
        if (ferror(stdin)) {
            free(buffer);
            fail(EXIT_IO, "stdin read failed");
        }
        if (feof(stdin))
            break;
    }
    *data = buffer;
    *length = used;
}

static ext2_ino_t ensure_directory(const char *path) {
    if (strcmp(path, "/") == 0)
        return EXT2_ROOT_INO;
    char copy[MAX_PATH_BYTES];
    strcpy(copy, path);
    ext2_ino_t current = EXT2_ROOT_INO;
    char *save = NULL;
    char *component = strtok_r(copy + 1, "/", &save);
    while (component != NULL) {
        ext2_ino_t next = 0;
        errcode_t rc = ext2fs_lookup(fs, current, component, (int)strlen(component),
                                     NULL, &next);
        if (rc == 0) {
            struct ext2_inode inode;
            rc = ext2fs_read_inode(fs, next, &inode);
            if (rc != 0)
                fail_rc(EXIT_IO, "read-directory", rc);
            if (!LINUX_S_ISDIR(inode.i_mode))
                fail(EXIT_OPERATION, "mkdir -p encountered a non-directory");
            current = next;
        } else {
            /* The fourth argument is inode flags; permissions use fs->umask. */
            rc = ext2fs_mkdir2(fs, current, 0, 0, 0, component, &next);
            if (rc != 0)
                fail_rc(EXIT_IO, "mkdir", rc);
            current = next;
        }
        component = strtok_r(NULL, "/", &save);
    }
    return current;
}

static int free_block(ext2_filsys volume, blk64_t *block, e2_blkcnt_t block_count,
                      blk64_t ref_block, int ref_offset, void *priv_data);

/* Give a regular-file inode an empty extent tree and mark it extent-mapped.
 *
 * libext2fs leaves a hand-built inode block-mapped, which the kernel reads
 * happily and UEFI does not: the read-only EXT4 driver the BDS carries
 * (Ext4Pkg) refuses any inode without EXT4_EXTENTS_FL outright, so a
 * block-mapped canoe.cfg or boot_a.efi opens and then fails every read with
 * Unsupported, which the firmware's LoadImage reports as Not Found. Directories
 * already arrive extent-mapped from ext2fs_mkdir2, which is why path lookup
 * survived while every file this helper wrote was unreadable to the loader.
 * The layout is the one e2fsprogs' own writers install: a depth-0 header whose
 * capacity is however many extents fit in i_block. */
static void mark_extent_mapped(struct ext2_inode *metadata) {
    struct ext3_extent_header *header;

    memset(metadata->i_block, 0, sizeof(metadata->i_block));
    header = (struct ext3_extent_header *)&metadata->i_block[0];
    header->eh_magic = ext2fs_cpu_to_le16(EXT3_EXT_MAGIC);
    header->eh_depth = 0;
    header->eh_entries = 0;
    header->eh_max = ext2fs_cpu_to_le16(
        (sizeof(metadata->i_block) - sizeof(*header)) / sizeof(struct ext3_extent));
    metadata->i_flags |= EXT4_EXTENTS_FL;
}

static void create_or_overwrite(const char *path, const unsigned char *data,
                                size_t length, bool parents) {
    char parent_path[MAX_PATH_BYTES];
    char name[MAX_COMPONENT_BYTES + 1];
    split_parent(path, parent_path, name);
    ext2_ino_t parent = parents ? ensure_directory(parent_path) : lookup_path(parent_path, false);
    struct ext2_inode parent_inode;
    errcode_t rc = ext2fs_read_inode(fs, parent, &parent_inode);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-parent", rc);
    if (!LINUX_S_ISDIR(parent_inode.i_mode))
        fail(EXIT_OPERATION, "parent is not a directory");

    ext2_ino_t inode = lookup_path(path, true);
    if (inode != 0) {
        struct ext2_inode metadata;
        rc = ext2fs_read_inode(fs, inode, &metadata);
        if (rc != 0)
            fail_rc(EXIT_IO, "read-target", rc);
        if (!LINUX_S_ISREG(metadata.i_mode))
            fail(EXIT_OPERATION, "write target is not a regular file");
        /* A generation written before this helper set the flag stays
         * block-mapped through an ordinary overwrite, so the loader would still
         * refuse to read it. Release its blocks and re-map it here rather than
         * leaving a file only the kernel can read.
         *
         * An inline-data target keeps its bytes in the inode and has no block
         * mapping to release, and a volume carrying that feature is refused by
         * the loader's driver at mount rather than at read, so overwriting one
         * in place stays correct here. */
        if (ext2fs_has_feature_extents(fs->super) &&
            (metadata.i_flags & (EXT4_EXTENTS_FL | EXT4_INLINE_DATA_FL)) == 0) {
            rc = ext2fs_block_iterate3(fs, inode, BLOCK_FLAG_DEPTH_TRAVERSE, NULL,
                                       free_block, NULL);
            if (rc != 0)
                fail_rc(EXIT_IO, "free-block-mapped-target", rc);
            metadata.i_size = 0;
            metadata.i_size_high = 0;
            metadata.i_blocks = 0;
            mark_extent_mapped(&metadata);
            rc = ext2fs_write_inode(fs, inode, &metadata);
            if (rc != 0)
                fail_rc(EXIT_IO, "remap-target-inode", rc);
        }
    } else {
        rc = ext2fs_new_inode(fs, parent, LINUX_S_IFREG | 0644, fs->inode_map, &inode);
        if (rc != 0)
            fail_rc(EXIT_IO, "new-inode", rc);
        ext2fs_inode_alloc_stats2(fs, inode, 1, 0);
        struct ext2_inode metadata;
        memset(&metadata, 0, sizeof(metadata));
        metadata.i_mode = LINUX_S_IFREG | 0644;
        metadata.i_links_count = 1;
        if (ext2fs_has_feature_extents(fs->super))
            mark_extent_mapped(&metadata);
        rc = ext2fs_write_new_inode(fs, inode, &metadata);
        if (rc != 0)
            fail_rc(EXIT_IO, "write-new-inode", rc);
        rc = ext2fs_link(fs, parent, name, inode, EXT2_FT_REG_FILE);
        if (rc != 0)
            fail_rc(EXIT_IO, "link-new-file", rc);
    }

    ext2_file_t file;
    rc = ext2fs_file_open(fs, inode, EXT2_FILE_WRITE, &file);
    if (rc != 0)
        fail_rc(EXIT_IO, "write-open", rc);
    rc = ext2fs_file_set_size2(file, 0);
    if (rc != 0) {
        (void)ext2fs_file_close(file);
        fail_rc(EXIT_IO, "truncate", rc);
    }
    size_t offset = 0;
    while (offset < length) {
        unsigned int wanted = length - offset > UINT_MAX ? UINT_MAX : (unsigned int)(length - offset);
        unsigned int written = 0;
        rc = ext2fs_file_write(file, data + offset, wanted, &written);
        if (rc != 0 || written != wanted) {
            (void)ext2fs_file_close(file);
            if (rc != 0)
                fail_rc(EXIT_IO, "write", rc);
            fail(EXIT_IO, "short filesystem write");
        }
        offset += written;
    }
    rc = ext2fs_file_flush(file);
    if (rc != 0) {
        (void)ext2fs_file_close(file);
        fail_rc(EXIT_IO, "write-flush", rc);
    }
    rc = ext2fs_file_close(file);
    if (rc != 0)
        fail_rc(EXIT_IO, "write-close", rc);
}

/* A staging-file allocation primitive. It never replaces an existing inode or
 * publishes a final boot-volume name. The caller owns promotion and recovery. */
static void validate_create(const char *path, size_t length, const char *reserve_text) {
    if (is_dirty(fs->super))
        fail(EXIT_DIRTY, "create requires a clean filesystem");
    char *end = NULL;
    errno = 0;
    if (*reserve_text < '0' || *reserve_text > '9')
        fail(EXIT_USAGE, "reserve must be an unsigned byte count");
    uint64_t reserve = strtoull(reserve_text, &end, 10);
    if (errno != 0 || *end != '\0')
        fail(EXIT_USAGE, "reserve must be an unsigned byte count");
    if (lookup_path(path, true) != 0)
        fail(EXIT_OPERATION, "create target already exists");
    if (!ext2fs_has_feature_extents(fs->super))
        fail(EXIT_UNSUPPORTED, "staging allocation requires extents");
    if (fs->super->s_free_inodes_count == 0)
        fail(EXIT_OPERATION, "no free inode for staging file");
    uint64_t block_size = fs->blocksize;
    uint64_t data_blocks = (length + block_size - 1) / block_size;
    uint64_t reserve_blocks = reserve / block_size + (reserve % block_size != 0);
    /* Worst-case one extent per data block, including all index levels and
     * a directory expansion. This is separate from the caller's free reserve. */
    uint64_t metadata_blocks = 1;
    uint64_t entries = data_blocks;
    uint64_t per_node = (block_size - sizeof(struct ext3_extent_header)) /
                        sizeof(struct ext3_extent);
    do {
        entries = (entries + per_node - 1) / per_node;
        metadata_blocks += entries;
    } while (entries > 1);
    uint64_t free_blocks = ext2fs_free_blocks_count(fs->super);
    uint64_t reserved_blocks = ext2fs_r_blocks_count(fs->super);
    uint64_t usable = free_blocks > reserved_blocks ? free_blocks - reserved_blocks : 0;
    if (reserve_blocks > usable || data_blocks > usable - reserve_blocks ||
        metadata_blocks > usable - reserve_blocks - data_blocks)
        fail(EXIT_OPERATION, "insufficient allocatable space with requested reserve");
}

struct empty_directory {
    bool nonempty;
};

static int inspect_empty_directory(ext2_ino_t dir, int entry,
                                   struct ext2_dir_entry *dirent, int offset,
                                   int blocksize, char *buf, void *priv_data) {
    (void)dir;
    (void)entry;
    (void)offset;
    (void)blocksize;
    (void)buf;
    struct empty_directory *result = priv_data;
    int length = ext2fs_dirent_name_len(dirent);
    if (dirent->inode != 0 && !(length == 1 && dirent->name[0] == '.') &&
        !(length == 2 && dirent->name[0] == '.' && dirent->name[1] == '.'))
        result->nonempty = true;
    return result->nonempty ? DIRENT_ABORT : 0;
}

static int free_block(ext2_filsys volume, blk64_t *block, e2_blkcnt_t block_count,
                      blk64_t ref_block, int ref_offset, void *priv_data) {
    (void)block_count;
    (void)ref_block;
    (void)ref_offset;
    (void)priv_data;
    if (*block != 0)
        ext2fs_block_alloc_stats2(volume, *block, -1);
    *block = 0;
    return BLOCK_CHANGED;
}

static void release_inode(ext2_ino_t inode, struct ext2_inode *metadata) {
    errcode_t rc;
    bool is_directory = LINUX_S_ISDIR(metadata->i_mode);
    if ((metadata->i_flags & EXT4_INLINE_DATA_FL) == 0) {
        rc = ext2fs_block_iterate3(fs, inode, BLOCK_FLAG_DEPTH_TRAVERSE, NULL,
                                   free_block, NULL);
        if (rc != 0)
            fail_rc(EXIT_IO, "free-blocks", rc);
    }
    metadata->i_links_count = 0;
    metadata->i_dtime = (uint32_t)time(NULL);
    rc = ext2fs_write_inode(fs, inode, metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "write-deleted-inode", rc);
    ext2fs_inode_alloc_stats2(fs, inode, -1, is_directory);
}

static void remove_path(const char *path) {
    char parent_path[MAX_PATH_BYTES];
    char name[MAX_COMPONENT_BYTES + 1];
    split_parent(path, parent_path, name);
    ext2_ino_t parent = lookup_path(parent_path, false);
    ext2_ino_t inode = lookup_path(path, false);
    struct ext2_inode metadata;
    errcode_t rc = ext2fs_read_inode(fs, inode, &metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-target", rc);
    bool is_directory = LINUX_S_ISDIR(metadata.i_mode);
    if (is_directory) {
        struct empty_directory empty = {false};
        rc = ext2fs_dir_iterate2(fs, inode, 0, NULL, inspect_empty_directory, &empty);
        if (rc != 0 && rc != DIRENT_ABORT)
            fail_rc(EXIT_IO, "inspect-directory", rc);
        if (empty.nonempty)
            fail(EXIT_OPERATION, "remove refuses non-empty directories");
    }
    rc = ext2fs_unlink(fs, parent, name, inode, 0);
    if (rc != 0)
        fail_rc(EXIT_IO, "unlink", rc);
    if (!is_directory) {
        /* ext2fs_unlink removes only the directory entry.  Apply the inode
         * link-count transition ourselves so ordinary files are reclaimed,
         * while hard-linked files retain their remaining data. */
        if (metadata.i_links_count > 1) {
            --metadata.i_links_count;
            rc = ext2fs_write_inode(fs, inode, &metadata);
            if (rc != 0)
                fail_rc(EXIT_IO, "write-unlinked-inode", rc);
            return;
        }
    }
    if (is_directory) {
        struct ext2_inode parent_metadata;
        rc = ext2fs_read_inode(fs, parent, &parent_metadata);
        if (rc != 0)
            fail_rc(EXIT_IO, "read-parent", rc);
        if (parent_metadata.i_links_count > 0)
            --parent_metadata.i_links_count;
        rc = ext2fs_write_inode(fs, parent, &parent_metadata);
        if (rc != 0)
            fail_rc(EXIT_IO, "write-parent", rc);
    }
    release_inode(inode, &metadata);
}

static void rename_path(const char *old_path, const char *new_path) {
    char old_parent_path[MAX_PATH_BYTES];
    char old_name[MAX_COMPONENT_BYTES + 1];
    char new_parent_path[MAX_PATH_BYTES];
    char new_name[MAX_COMPONENT_BYTES + 1];
    split_parent(old_path, old_parent_path, old_name);
    split_parent(new_path, new_parent_path, new_name);
    ext2_ino_t old_parent = lookup_path(old_parent_path, false);
    ext2_ino_t new_parent = lookup_path(new_parent_path, false);
    ext2_ino_t inode = lookup_path(old_path, false);
    if (lookup_path(new_path, true) != 0)
        fail(EXIT_OPERATION, "rename destination already exists");
    struct ext2_inode metadata;
    errcode_t rc = ext2fs_read_inode(fs, inode, &metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-rename-target", rc);
    if (LINUX_S_ISDIR(metadata.i_mode))
        fail(EXIT_OPERATION, "rename of directories is unsupported");
    rc = ext2fs_link(fs, new_parent, new_name, inode, EXT2_FT_REG_FILE);
    if (rc != 0)
        fail_rc(EXIT_IO, "link-rename-target", rc);
    rc = ext2fs_unlink(fs, old_parent, old_name, inode, 0);
    if (rc != 0)
        fail_rc(EXIT_IO, "unlink-rename-source", rc);
}

static const char *type_name(int type) {
    switch (type) {
    case EXT2_FT_REG_FILE: return "file";
    case EXT2_FT_DIR: return "directory";
    case EXT2_FT_SYMLINK: return "symlink";
    case EXT2_FT_CHRDEV: return "char_device";
    case EXT2_FT_BLKDEV: return "block_device";
    case EXT2_FT_FIFO: return "fifo";
    case EXT2_FT_SOCK: return "socket";
    default: return "unknown";
    }
}

static int list_entry(ext2_ino_t dir, int entry, struct ext2_dir_entry *dirent,
                      int offset, int blocksize, char *buf, void *priv_data) {
    (void)dir;
    (void)entry;
    (void)offset;
    (void)blocksize;
    (void)buf;
    bool *first = priv_data;
    int length = ext2fs_dirent_name_len(dirent);
    if (dirent->inode == 0 || (length == 1 && dirent->name[0] == '.') ||
        (length == 2 && dirent->name[0] == '.' && dirent->name[1] == '.'))
        return 0;
    char name[MAX_COMPONENT_BYTES + 1];
    memcpy(name, dirent->name, (size_t)length);
    name[length] = '\0';
    if (!*first)
        putchar(',');
    *first = false;
    printf("{\"name\":");
    print_json_string(name);
    printf(",\"inode\":%" PRIu32 ",\"type\":", ext2fs_le32_to_cpu(dirent->inode));
    print_json_string(type_name(ext2fs_dirent_file_type(dirent)));
    putchar('}');
    return 0;
}

static void inspect_filesystem(const char *path) {
    struct ext2_super_block *super = fs->super;
    uint64_t free_blocks = ext2fs_free_blocks_count(super);
    uint64_t reserved_blocks = ext2fs_r_blocks_count(super);
    uint64_t usable_blocks = free_blocks > reserved_blocks ? free_blocks - reserved_blocks : 0;
    uint64_t total_blocks = ext2fs_blocks_count(super);
    uint64_t block_size = fs->blocksize;
    printf("{\"state\":\"");
    fputs(is_dirty(super) ? "dirty" : "clean", stdout);
    printf("\",\"total_blocks\":%" PRIu64 ",\"total_bytes\":%" PRIu64
           ",\"free_blocks\":%" PRIu64 ",\"free_bytes\":%" PRIu64,
           total_blocks, total_blocks * block_size, free_blocks,
           free_blocks * block_size);
    printf(",\"allocatable_bytes\":%" PRIu64, usable_blocks * block_size);
    printf(",\"free_inodes\":%" PRIu32 ",\"block_size\":%" PRIu64
           ",\"features\":{\"compat\":%" PRIu32,
           super->s_free_inodes_count, block_size, super->s_feature_compat);
    printf(",\"ro_compat\":%" PRIu32 ",\"incompat\":%" PRIu32,
           super->s_feature_ro_compat, super->s_feature_incompat);
    fputs(",\"compat_names\":", stdout);
    print_feature_list(super->s_feature_compat, feature_name_compat);
    fputs(",\"ro_compat_names\":", stdout);
    print_feature_list(super->s_feature_ro_compat, feature_name_ro);
    fputs(",\"incompat_names\":", stdout);
    print_feature_list(super->s_feature_incompat, feature_name_incompat);
    fputs("}", stdout);
    if (path != NULL) {
        ext2_ino_t inode = lookup_path(path, true);
        printf(",\"path\":");
        print_json_string(path);
        printf(",\"path_exists\":%s", inode == 0 ? "false" : "true");
    }
    puts("}");
}

static int compare_block(const void *a, const void *b) {
    blk64_t x = *(const blk64_t *)a, y = *(const blk64_t *)b;
    return x < y ? -1 : x > y;
}

/* Allocation evidence, not an authorization to write physical extents. Firmware
 * independently validates its mapping and ownership immediately before use. */
static void inspect_allocation(const char *path) {
    if (is_dirty(fs->super))
        fail(EXIT_DIRTY, "allocation inspection requires a clean filesystem");
    ext2_ino_t number = lookup_path(path, false);
    struct ext2_inode inode;
    errcode_t rc = ext2fs_read_inode(fs, number, &inode);
    if (rc != 0)
        fail_rc(EXIT_IO, "allocation-inode", rc);
    if (!LINUX_S_ISREG(inode.i_mode) || inode.i_links_count != 1 ||
        inode.i_flags != EXT4_EXTENTS_FL)
        fail(EXIT_UNSUPPORTED, "allocation requires a single-link plain extent file");
    uint64_t bytes = EXT2_I_SIZE(&inode);
    if (bytes == 0 || bytes > MAX_WRITE_BYTES || bytes % fs->blocksize != 0)
        fail(EXIT_OPERATION, "allocation requires a bounded block-aligned file");
    size_t count = bytes / fs->blocksize;
    blk64_t *blocks = calloc(count, sizeof(*blocks));
    blk64_t *sorted = calloc(count, sizeof(*sorted));
    if (blocks == NULL || sorted == NULL)
        fail(EXIT_IO, "cannot allocate extent inspection buffers");
    rc = ext2fs_read_block_bitmap(fs);
    if (rc != 0)
        fail_rc(EXIT_IO, "allocation-bitmap", rc);
    for (size_t logical = 0; logical < count; logical++) {
        int flags = 0;
        rc = ext2fs_bmap2(fs, number, &inode, NULL, 0, logical, &flags, &blocks[logical]);
        if (rc != 0)
            fail_rc(EXIT_IO, "allocation-map", rc);
        if (flags != 0 || blocks[logical] == 0 ||
            blocks[logical] >= ext2fs_blocks_count(fs->super) ||
            !ext2fs_test_block_bitmap2(fs->block_map, blocks[logical]))
            fail(EXIT_OPERATION, "file contains a hole, unwritten or unallocated extent");
        sorted[logical] = blocks[logical];
    }
    qsort(sorted, count, sizeof(*sorted), compare_block);
    for (size_t i = 1; i < count; i++)
        if (sorted[i] == sorted[i - 1])
            fail(EXIT_OPERATION, "file contains overlapping physical blocks");
    printf("{\"bytes\":%" PRIu64 ",\"block_size\":%u,\"initialized\":true,\"extents\":[",
           bytes, fs->blocksize);
    for (size_t first = 0; first < count;) {
        size_t next = first + 1;
        while (next < count && blocks[next] == blocks[next - 1] + 1)
            next++;
        printf("%s{\"logical_block\":%zu,\"physical_block\":%" PRIu64 ",\"blocks\":%zu}",
               first == 0 ? "" : ",", first, (uint64_t)blocks[first], next - first);
        first = next;
    }
    puts("]}");
    free(sorted);
    free(blocks);
}

static void list_directory(const char *path) {
    ext2_ino_t inode = lookup_path(path, false);
    struct ext2_inode metadata;
    errcode_t rc = ext2fs_read_inode(fs, inode, &metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-directory", rc);
    if (!LINUX_S_ISDIR(metadata.i_mode))
        fail(EXIT_OPERATION, "list target is not a directory");
    bool first = true;
    putchar('[');
    rc = ext2fs_dir_iterate2(fs, inode, 0, NULL, list_entry, &first);
    if (rc != 0)

        fail_rc(EXIT_IO, "list", rc);
    puts("]");
}
enum transaction_entry_kind {
    ENTRY_ABSENT,
    ENTRY_DIRECTORY,
    ENTRY_FILE,
    ENTRY_UNOBSERVED,
};

struct transaction_entry {
    enum transaction_entry_kind desired;
    enum transaction_entry_kind expected;
    enum transaction_entry_kind actual;
    char *target;
    char *local;
    unsigned char *snapshot;
    size_t snapshot_length;
    bool absent_removed;
};

struct transaction {
    struct transaction_entry *entries;
    size_t count;
    size_t snapshot_bytes;
};

static void free_transaction(struct transaction *transaction) {
    for (size_t index = 0; index < transaction->count; ++index) {
        free(transaction->entries[index].target);
        free(transaction->entries[index].local);
        free(transaction->entries[index].snapshot);
    }
    free(transaction->entries);
    transaction->entries = NULL;
    transaction->count = 0;
    transaction->snapshot_bytes = 0;
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

static char *decode_hex(const char *encoded) {
    size_t length = strlen(encoded);
    if (length == 0 || (length & 1U) != 0 || length / 2 >= MAX_PATH_BYTES)
        fail(EXIT_USAGE, "sync manifest has an invalid encoded path");
    char *decoded = malloc(length / 2 + 1);
    if (decoded == NULL)
        fail(EXIT_IO, "out of memory decoding sync manifest");
    for (size_t index = 0; index < length; index += 2) {
        int high = hex_value(encoded[index]);
        int low = hex_value(encoded[index + 1]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) {
            free(decoded);
            fail(EXIT_USAGE, "sync manifest has an invalid encoded path");
        }
        decoded[index / 2] = (char)((high << 4) | low);
    }
    decoded[length / 2] = '\0';
    return decoded;
}

static char *copy_normalized_path(const char *encoded) {
    char *decoded = decode_hex(encoded);
    char normalized[MAX_PATH_BYTES];
    normalize_path(decoded, normalized);
    free(decoded);
    char *result = strdup(normalized);
    if (result == NULL)
        fail(EXIT_IO, "out of memory decoding sync manifest");
    return result;
}

static char *copy_normalized_relative_path(const char *encoded) {
    char *decoded = decode_hex(encoded);
    if (decoded[0] == '/') {
        free(decoded);
        fail(EXIT_USAGE, "sync manifest local path must be relative");
    }
    char absolute[MAX_PATH_BYTES];
    int written = snprintf(absolute, sizeof(absolute), "/%s", decoded);
    free(decoded);
    if (written < 0 || (size_t)written >= sizeof(absolute))
        fail(EXIT_USAGE, "sync manifest local path is too long");
    char normalized[MAX_PATH_BYTES];
    normalize_path(absolute, normalized);
    char *result = strdup(normalized + 1);
    if (result == NULL)
        fail(EXIT_IO, "out of memory decoding sync manifest");
    return result;
}

static enum transaction_entry_kind parse_entry_kind(char value, bool expected) {
    switch (value) {
    case 'a': return ENTRY_ABSENT;
    case 'd': return ENTRY_DIRECTORY;
    case 'f': return ENTRY_FILE;
    case 'u':
        if (expected)
            return ENTRY_UNOBSERVED;
        break;
    default:
        break;
    }
    fail(EXIT_USAGE, "sync manifest has an invalid entry state");
    return ENTRY_ABSENT;
}

static void parse_sync_manifest(const char *manifest, struct transaction *transaction) {
    FILE *stream = fopen(manifest, "r");
    if (stream == NULL)
        fail(EXIT_IO, "cannot open sync manifest");
    if (fseek(stream, 0, SEEK_END) != 0)
        fail(EXIT_IO, "cannot size sync manifest");
    long manifest_bytes = ftell(stream);
    if (manifest_bytes < 0 || (unsigned long)manifest_bytes > MAX_SYNC_MANIFEST_BYTES)
        fail(EXIT_OPERATION, "sync manifest exceeds the size limit");
    if (fseek(stream, 0, SEEK_SET) != 0)
        fail(EXIT_IO, "cannot rewind sync manifest");
    char line[MAX_SYNC_MANIFEST_LINE_BYTES + 1];
    char *previous = NULL;
    while (fgets(line, sizeof(line), stream) != NULL) {
        size_t length = strlen(line);
        if (length == 0 || line[length - 1] != '\n')
            fail(EXIT_USAGE, "sync manifest has an unterminated line");
        line[length - 1] = '\0';
        char *fields[5] = {0};
        char *save = NULL;
        size_t field_count = 0;
        for (char *field = strtok_r(line, " ", &save); field != NULL;
             field = strtok_r(NULL, " ", &save)) {
            if (field_count == 4)
                fail(EXIT_USAGE, "sync manifest has too many fields");
            fields[field_count++] = field;
        }
        if (field_count != 4 || strlen(fields[0]) != 1 || strlen(fields[1]) != 1)
            fail(EXIT_USAGE, "sync manifest has an invalid line");
        if (transaction->count == MAX_TRANSACTION_ENTRIES)
            fail(EXIT_OPERATION, "sync manifest exceeds the entry limit");
        struct transaction_entry entry = {
            .desired = parse_entry_kind(fields[0][0], false),
            .expected = parse_entry_kind(fields[1][0], true),
            .actual = ENTRY_UNOBSERVED,
            .target = copy_normalized_path(fields[2]),
            .local = copy_normalized_relative_path(fields[3]),
            .snapshot = NULL,
            .snapshot_length = 0,
            .absent_removed = false,
        };
        if (previous != NULL && strcmp(previous, entry.target) >= 0) {
            free(entry.target);
            free(entry.local);
            fail(EXIT_USAGE, "sync manifest paths must be strictly ordered");
        }
        struct transaction_entry *grown =
            realloc(transaction->entries, (transaction->count + 1) * sizeof(*grown));
        if (grown == NULL) {
            free(entry.target);
            free(entry.local);
            fail(EXIT_IO, "out of memory reading sync manifest");
        }
        transaction->entries = grown;
        transaction->entries[transaction->count++] = entry;
        previous = entry.target;
    }
    if (ferror(stream)) {
        (void)fclose(stream);
        fail(EXIT_IO, "cannot read sync manifest");
    }
    if (fclose(stream) != 0)
        fail(EXIT_IO, "cannot close sync manifest");
    if (transaction->count == 0)
        fail(EXIT_USAGE, "sync manifest is empty");
}

static void read_filesystem_file(const char *path, unsigned char **data, size_t *length) {
    ext2_ino_t inode = lookup_path(path, false);
    struct ext2_inode metadata;
    errcode_t rc = ext2fs_read_inode(fs, inode, &metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-sync-inode", rc);
    if (!LINUX_S_ISREG(metadata.i_mode))
        fail(EXIT_OPERATION, "sync snapshot path is not a regular file");
    ext2_file_t file;
    rc = ext2fs_file_open(fs, inode, 0, &file);
    if (rc != 0)
        fail_rc(EXIT_IO, "open-sync-snapshot", rc);
    __u64 file_length = 0;
    rc = ext2fs_file_get_lsize(file, &file_length);
    if (rc != 0 || file_length > MAX_WRITE_BYTES) {
        (void)ext2fs_file_close(file);
        if (rc != 0)
            fail_rc(EXIT_IO, "size-sync-snapshot", rc);
        fail(EXIT_OPERATION, "sync snapshot file exceeds the 64 MiB limit");
    }
    unsigned char *bytes = NULL;
    if (file_length != 0) {
        bytes = malloc((size_t)file_length);
        if (bytes == NULL) {
            (void)ext2fs_file_close(file);
            fail(EXIT_IO, "out of memory reading sync snapshot");
        }
    }
    size_t offset = 0;
    while (offset < (size_t)file_length) {
        unsigned int got = 0;
        unsigned int wanted = (size_t)file_length - offset > UINT_MAX
                                  ? UINT_MAX
                                  : (unsigned int)((size_t)file_length - offset);
        rc = ext2fs_file_read(file, bytes + offset, wanted, &got);
        if (rc != 0 || got == 0) {
            free(bytes);
            (void)ext2fs_file_close(file);
            if (rc != 0)
                fail_rc(EXIT_IO, "read-sync-snapshot", rc);
            fail(EXIT_IO, "short sync snapshot read");
        }
        offset += got;
    }
    rc = ext2fs_file_close(file);
    if (rc != 0) {
        free(bytes);
        fail_rc(EXIT_IO, "close-sync-snapshot", rc);
    }
    *data = bytes;
    *length = (size_t)file_length;
}

static void build_host_path(const char *root, const char *relative,
                            char path[MAX_PATH_BYTES]) {
    size_t root_length = strlen(root);
    size_t relative_length = strlen(relative);
    if (root_length == 0 || root_length >= MAX_PATH_BYTES ||
        relative_length == 0 ||
        root_length + relative_length + 2 > MAX_PATH_BYTES)
        fail(EXIT_USAGE, "sync manifest local path is too long");
    memcpy(path, root, root_length);
    size_t length = root_length;
    if (path[length - 1] != '/')
        path[length++] = '/';
    memcpy(path + length, relative, relative_length + 1);
}

static bool host_path_is_directory_no_symlink(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        fail(EXIT_IO, "cannot stat sync local entry");
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        fail(EXIT_OPERATION, "sync local path crosses a symlink");
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat metadata;
    if (lstat(path, &metadata) < 0)
        fail(EXIT_IO, "cannot stat sync local entry");
    if (S_ISLNK(metadata.st_mode))
        fail(EXIT_OPERATION, "sync local path crosses a symlink");
    return S_ISDIR(metadata.st_mode);
#endif
}

static bool host_path_is_regular_no_symlink(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        fail(EXIT_IO, "cannot stat sync local entry");
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        fail(EXIT_OPERATION, "sync local path crosses a symlink");
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat metadata;
    if (lstat(path, &metadata) < 0)
        fail(EXIT_IO, "cannot stat sync local entry");
    if (S_ISLNK(metadata.st_mode))
        fail(EXIT_OPERATION, "sync local path crosses a symlink");
    return S_ISREG(metadata.st_mode);
#endif
}

static bool validate_host_file_path(const char *root, const char *relative,
                                    char path[MAX_PATH_BYTES]) {
    build_host_path(root, relative, path);
    if (!host_path_is_directory_no_symlink(root))
        fail(EXIT_IO, "sync local root is not a directory");
    size_t root_length = strlen(root);
    char *component = path + root_length;
    if (*component == '/')
        ++component;
    bool is_directory = false;
    while (component != NULL && *component != '\0') {
        char *next = strchr(component, '/');
        if (next != NULL)
            *next = '\0';
        is_directory = host_path_is_directory_no_symlink(path);
        if (next != NULL && !is_directory)
            fail(EXIT_OPERATION, "sync local path crosses a non-directory");
        if (next != NULL) {
            *next = '/';
            component = next + 1;
        } else {
            component = NULL;
        }
    }
    return is_directory;
}

static void host_file_bytes(const char *root, const char *relative, unsigned char **data,
                            size_t *length) {
    char path[MAX_PATH_BYTES];
    validate_host_file_path(root, relative, path);
    FILE *stream = fopen(path, "rb");
    if (stream == NULL)
        fail(EXIT_IO, "cannot open sync local file");
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        fail(EXIT_IO, "cannot size sync local file");
    }
    long file_length = ftell(stream);
    if (file_length < 0 || (unsigned long)file_length > MAX_WRITE_BYTES) {
        fclose(stream);
        fail(EXIT_OPERATION, "sync local file exceeds the 64 MiB limit");
    }
    if (fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        fail(EXIT_IO, "cannot rewind sync local file");
    }
    unsigned char *bytes = NULL;
    if (file_length != 0) {
        bytes = malloc((size_t)file_length);
        if (bytes == NULL) {
            fclose(stream);
            fail(EXIT_IO, "out of memory reading sync local file");
        }
        if (fread(bytes, 1, (size_t)file_length, stream) != (size_t)file_length) {
            free(bytes);
            fclose(stream);
            fail(EXIT_IO, "cannot read sync local file");
        }
    }
    if (fclose(stream) != 0) {
        free(bytes);
        fail(EXIT_IO, "cannot close sync local file");
    }
    *data = bytes;
    *length = (size_t)file_length;
}

static void validate_host_entry(const char *root, const struct transaction_entry *entry) {
    if (entry->desired == ENTRY_ABSENT)
        return;
    char path[MAX_PATH_BYTES];
    bool is_directory = validate_host_file_path(root, entry->local, path);
    if ((entry->desired == ENTRY_FILE &&
         (!host_path_is_regular_no_symlink(path) || is_directory)) ||
        (entry->desired == ENTRY_DIRECTORY && !is_directory))
        fail(EXIT_OPERATION, "sync manifest local entry has the wrong type");
}

static ext2_ino_t lookup_sync_path(const char *path) {
    ext2_ino_t inode = 0;
    errcode_t rc = ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, &inode);
    if (rc == EXT2_ET_FILE_NOT_FOUND)
        return 0;
    if (rc != 0)
        fail_rc(EXIT_IO, "lookup-sync-path", rc);
    return inode;
}

static void validate_source_path(const char *path) {
    char copy[MAX_PATH_BYTES];
    strcpy(copy, path);
    ext2_ino_t current = EXT2_ROOT_INO;
    char *save = NULL;
    char *component = strtok_r(copy + 1, "/", &save);
    while (component != NULL) {
        ext2_ino_t next = 0;
        errcode_t rc = ext2fs_lookup(fs, current, component, (int)strlen(component),
                                     NULL, &next);
        if (rc == EXT2_ET_FILE_NOT_FOUND)
            return;
        if (rc != 0)
            fail_rc(EXIT_IO, "lookup-sync-component", rc);
        struct ext2_inode metadata;
        rc = ext2fs_read_inode(fs, next, &metadata);
        if (rc != 0)
            fail_rc(EXIT_IO, "read-sync-path", rc);
        if (LINUX_S_ISLNK(metadata.i_mode))
            fail(EXIT_OPERATION, "sync path crosses a symlink");
        char *next_component = strtok_r(NULL, "/", &save);
        if (next_component != NULL && !LINUX_S_ISDIR(metadata.i_mode))
            fail(EXIT_OPERATION, "sync path crosses a non-directory");
        current = next;
        component = next_component;
    }
}

static void snapshot_entry(struct transaction *transaction,
                           struct transaction_entry *entry) {
    validate_source_path(entry->target);
    ext2_ino_t inode = lookup_sync_path(entry->target);
    if (inode == 0) {
        entry->actual = ENTRY_ABSENT;
        return;
    }
    struct ext2_inode metadata;
    errcode_t rc = ext2fs_read_inode(fs, inode, &metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "read-sync-entry", rc);
    if (LINUX_S_ISDIR(metadata.i_mode)) {
        entry->actual = ENTRY_DIRECTORY;
        return;
    }
    if (!LINUX_S_ISREG(metadata.i_mode))
        fail(EXIT_OPERATION, "sync snapshot path has an unsupported type");
    entry->actual = ENTRY_FILE;
    read_filesystem_file(entry->target, &entry->snapshot, &entry->snapshot_length);
    if (entry->snapshot_length > MAX_TRANSACTION_BYTES - transaction->snapshot_bytes)
        fail(EXIT_OPERATION, "sync snapshot exceeds the transaction memory limit");
    transaction->snapshot_bytes += entry->snapshot_length;
}

static void verify_expected_entry(const char *expected_root,
                                  const struct transaction_entry *entry) {
    if (entry->expected == ENTRY_UNOBSERVED)
        return;
    if (entry->actual != entry->expected)
        fail(EXIT_OPERATION, "source changed after ext4 extraction");
    if (entry->expected != ENTRY_FILE)
        return;
    unsigned char *expected = NULL;
    size_t expected_length = 0;
    host_file_bytes(expected_root, entry->local, &expected, &expected_length);
    bool equal = expected_length == entry->snapshot_length &&
                 (expected_length == 0 ||
                  memcmp(expected, entry->snapshot, expected_length) == 0);
    free(expected);
    if (!equal)
        fail(EXIT_OPERATION, "source changed after ext4 extraction");
}

static unsigned int path_depth(const char *path) {
    unsigned int depth = 0;
    for (const char *value = path; *value != '\0'; ++value) {
        if (*value == '/')
            ++depth;
    }
    return depth;
}

static void remove_absent_targets(struct transaction *transaction) {
    for (size_t count = transaction->count; count > 0; --count) {
        size_t selected = 0;
        unsigned int selected_depth = 0;
        for (size_t index = 0; index < transaction->count; ++index) {
            struct transaction_entry *entry = &transaction->entries[index];
            if (entry->desired != ENTRY_ABSENT || entry->actual == ENTRY_ABSENT ||
                entry->absent_removed)
                continue;
            unsigned int depth = path_depth(entry->target);
            if (depth >= selected_depth) {
                selected = index;
                selected_depth = depth;
            }
        }
        if (selected_depth == 0)
            return;
        struct transaction_entry *entry = &transaction->entries[selected];
        entry->absent_removed = true;
        remove_path(entry->target);
    }
}

static void apply_sync(struct transaction *transaction, const char *desired_root) {
    remove_absent_targets(transaction);
    for (size_t index = 0; index < transaction->count; ++index) {
        struct transaction_entry *entry = &transaction->entries[index];
        if (entry->desired == ENTRY_DIRECTORY)
            (void)ensure_directory(entry->target);
    }
    for (size_t index = 0; index < transaction->count; ++index) {
        struct transaction_entry *entry = &transaction->entries[index];
        if (entry->desired != ENTRY_FILE)
            continue;
        unsigned char *bytes = NULL;
        size_t length = 0;
        host_file_bytes(desired_root, entry->local, &bytes, &length);
        create_or_overwrite(entry->target, bytes, length, true);
        free(bytes);
    }
}

static void restore_sync(struct transaction *transaction) {
    for (size_t count = transaction->count; count > 0; --count) {
        size_t selected = 0;
        unsigned int selected_depth = 0;
        for (size_t index = 0; index < transaction->count; ++index) {
            struct transaction_entry *entry = &transaction->entries[index];
            if (entry->actual != ENTRY_ABSENT)
                continue;
            unsigned int depth = path_depth(entry->target);
            if (depth >= selected_depth) {
                selected = index;
                selected_depth = depth;
            }
        }
        if (selected_depth == 0)
            break;
        struct transaction_entry *entry = &transaction->entries[selected];
        entry->actual = ENTRY_UNOBSERVED;
        if (lookup_sync_path(entry->target) != 0)
            remove_path(entry->target);
    }
    for (size_t index = 0; index < transaction->count; ++index) {
        struct transaction_entry *entry = &transaction->entries[index];
        if (entry->actual == ENTRY_FILE)
            create_or_overwrite(entry->target, entry->snapshot, entry->snapshot_length, true);
    }
    for (size_t index = 0; index < transaction->count; ++index) {
        struct transaction_entry *entry = &transaction->entries[index];
        if (entry->actual == ENTRY_DIRECTORY)
            (void)ensure_directory(entry->target);
    }
}

static int sync_filesystem(const char *manifest, const char *desired_root,
                            const char *expected_root) {
    struct transaction transaction = {0};
    parse_sync_manifest(manifest, &transaction);
    for (size_t index = 0; index < transaction.count; ++index)
        validate_host_entry(desired_root, &transaction.entries[index]);
    for (size_t index = 0; index < transaction.count; ++index) {
        snapshot_entry(&transaction, &transaction.entries[index]);
        verify_expected_entry(expected_root, &transaction.entries[index]);
    }

    transaction_phase = TRANSACTION_APPLY;
    if (setjmp(transaction_jump) != 0) {
        if (transaction_phase == CANOE_TRANSACTION_ROLLBACK) {
            fprintf(stderr, "transaction_rollback=failed\n");
            transaction_phase = TRANSACTION_NONE;
            free_transaction(&transaction);
            close_quietly();
            return EXIT_OPERATION;
        }
        fprintf(stderr, "transaction_apply=failed\n");
        transaction_phase = CANOE_TRANSACTION_ROLLBACK;
        open_filesystem(true, true);
        restore_sync(&transaction);
        finish_sync_transaction();
        transaction_phase = TRANSACTION_NONE;
        free_transaction(&transaction);
        return transaction_error;
    }
    apply_sync(&transaction, desired_root);
    finish_sync_transaction();
    transaction_phase = TRANSACTION_NONE;
    free_transaction(&transaction);
    return EXIT_OK;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--recover] [--mkdir-p] [--path PATH] COMMAND ...\n"
            "commands:\n"
            "  inspect SOURCE [--path PATH]\n"
            "  read SOURCE PATH\n"
            "  allocation SOURCE PATH\n"
            "  write SOURCE PATH < STDIN\n"
            "  create SOURCE PATH MIN_FREE_BYTES < STDIN\n"
            "  mkdir SOURCE PATH\n"
            "  remove SOURCE PATH\n"
            "  rename SOURCE OLD_PATH NEW_PATH\n"
            "  sync SOURCE MANIFEST DESIRED_ROOT EXPECTED_ROOT\n"
            "  list SOURCE DIRECTORY\n"
            "exit codes: 0 ok, 2 usage, 3 unsupported feature, 4 dirty,\n"
            "           5 mounted, 6 I/O, 7 missing path, 8 operation error\n",
            argv0);
    exit(EXIT_USAGE);
}

static options_t parse_options(int argc, char **argv) {
    options_t options = {0};
    int index = 1;
    while (index < argc && argv[index][0] == '-') {
        if (strcmp(argv[index], "--recover") == 0)
            options.recover = true;
        else if (strcmp(argv[index], "--mkdir-p") == 0 || strcmp(argv[index], "-p") == 0)
            options.mkdir_p = true;
        else if (strcmp(argv[index], "--path") == 0) {
            if (++index >= argc)
                usage(argv[0]);
            options.inspect_path = argv[index];
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0)
            usage(argv[0]);
        else
            usage(argv[0]);
        ++index;
    }
    if (index >= argc)
        usage(argv[0]);
    options.command_index = index;
    return options;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* read/write/sync carry arbitrary firmware bytes over redirected streams. */
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1)
        fail(EXIT_IO, "cannot set binary standard streams");
#endif
    initialize_ext2_error_table();
    options_t options = parse_options(argc, argv);
    const char *command = argv[options.command_index];
    int first_argument = options.command_index + 1;
    int argument_count = argc - first_argument;
    bool command_mutates = strcmp(command, "write") == 0 || strcmp(command, "mkdir") == 0 ||
                           strcmp(command, "remove") == 0 || strcmp(command, "rename") == 0 ||
                           strcmp(command, "sync") == 0 || strcmp(command, "create") == 0;
    if (strcmp(command, "inspect") != 0 && strcmp(command, "read") != 0 &&
        strcmp(command, "write") != 0 && strcmp(command, "mkdir") != 0 &&
        strcmp(command, "remove") != 0 && strcmp(command, "rename") != 0 &&
        strcmp(command, "sync") != 0 && strcmp(command, "list") != 0 &&
        strcmp(command, "create") != 0 && strcmp(command, "allocation") != 0)
        usage(argv[0]);

    bool trailing_inspect_path = strcmp(command, "inspect") == 0 && argument_count == 3 &&
                                 strcmp(argv[first_argument + 1], "--path") == 0;
    if (trailing_inspect_path)
        options.inspect_path = argv[first_argument + 2];
    int required = (strcmp(command, "rename") == 0 || strcmp(command, "create") == 0)
                       ? 3
                       : (strcmp(command, "sync") == 0 ? 4
                                                        : (strcmp(command, "inspect") == 0 ? 1 : 2));
    if (trailing_inspect_path)
        argument_count = 1;
    if (argument_count != required)
        usage(argv[0]);
    if (options.inspect_path != NULL && strcmp(command, "inspect") != 0)
        usage(argv[0]);
    if (options.mkdir_p && strcmp(command, "write") != 0 && strcmp(command, "mkdir") != 0)
        usage(argv[0]);

    const char *source = argv[first_argument];
    struct stat source_stat = {0};
    int open_flags = (command_mutates ? O_RDWR : O_RDONLY);
#ifdef _WIN32
    open_flags |= O_BINARY;
    /* Raw disks are device objects, not directory entries: stat/_open cannot
     * validate them. Keep a Win32 handle for the independent superblock probe
     * and final flush, just as libext2fs' windows_io_manager does for its IO. */
    HANDLE source_handle = CreateFileA(source,
        GENERIC_READ | (command_mutates ? GENERIC_WRITE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (source_handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "canoe-ext4: cannot open source (Windows error %lu)\n", GetLastError());
        return EXIT_IO;
    }
    source_fd = _open_osfhandle((intptr_t)source_handle, open_flags);
    if (source_fd < 0)
        CloseHandle(source_handle);
#else
    if (stat(source, &source_stat) < 0)
        fail(EXIT_IO, "cannot stat source");
    source_fd = open(source, open_flags);
#endif
    if (source_fd < 0)
        fail(EXIT_IO, "cannot open source");
    if (lock_source(source_fd) < 0) {
        int lock_error = errno;
        fprintf(stderr, "canoe-ext4: cannot acquire exclusive source lock: %s (errno=%d)\n",
                strerror(lock_error), lock_error);
        close_quietly();
        return EXIT_IO;
    }
    if (source_is_mounted(source, &source_stat))
        fail(EXIT_MOUNTED, "source is mounted");
    char canonical[PATH_MAX];
#ifdef _WIN32
    if (_fullpath(canonical, source, sizeof(canonical)) == NULL)
        fail(EXIT_IO, "cannot canonicalize source");
#else
    if (realpath(source, canonical) == NULL)
        fail(EXIT_IO, "cannot canonicalize source");
#endif
    source_name = canonical;

    char normalized_a[MAX_PATH_BYTES];
    char normalized_b[MAX_PATH_BYTES];
    const char *path_a = NULL;
    const char *path_b = NULL;
    if (strcmp(command, "sync") == 0) {
        path_a = NULL;
    } else if (strcmp(command, "inspect") == 0) {
        if (options.inspect_path != NULL) {
            normalize_path(options.inspect_path, normalized_a);
            path_a = normalized_a;
        }
    } else if (strcmp(command, "rename") == 0) {
        normalize_path(argv[first_argument + 1], normalized_a);
        normalize_path(argv[first_argument + 2], normalized_b);
        path_a = normalized_a;
        path_b = normalized_b;
    } else {
        normalize_path(argv[first_argument + 1], normalized_a);
        path_a = normalized_a;
    }

    mutating = command_mutates;
    open_filesystem(command_mutates && strcmp(command, "create") != 0, options.recover);
    if (strcmp(command, "inspect") == 0) {
        inspect_filesystem(path_a);
    } else if (strcmp(command, "allocation") == 0) {
        inspect_allocation(path_a);
    } else if (strcmp(command, "read") == 0) {
        read_file_to_stdout(path_a);
        if (fflush(stdout) != 0)
            fail(EXIT_IO, "stdout flush failed");
    } else if (strcmp(command, "write") == 0 || strcmp(command, "create") == 0) {
        unsigned char *data = NULL;
        size_t length = 0;
        read_stdin(&data, &length);
        if (strcmp(command, "create") == 0) {
            validate_create(path_a, length, argv[first_argument + 2]);
            errcode_t rc = ext2fs_close(fs);
            fs = NULL;
            if (rc != 0)
                fail_rc(EXIT_IO, "create-preflight-close", rc);
            /* source_fd retains its exclusive lock across this reopen. */
            open_filesystem(true, false);
            validate_create(path_a, length, argv[first_argument + 2]);
            create_or_overwrite(path_a, data, length, false);
        } else
            create_or_overwrite(path_a, data, length, options.mkdir_p);
        free(data);
    } else if (strcmp(command, "mkdir") == 0) {
        if (options.mkdir_p)
            (void)ensure_directory(path_a);
        else {
            char parent[MAX_PATH_BYTES];
            char name[MAX_COMPONENT_BYTES + 1];
            split_parent(path_a, parent, name);
            ext2_ino_t parent_inode = lookup_path(parent, false);
            errcode_t rc = ext2fs_mkdir2(fs, parent_inode, 0, 0, 0, name, NULL);
            if (rc != 0)
                fail_rc(EXIT_IO, "mkdir", rc);
        }
    } else if (strcmp(command, "remove") == 0) {
        remove_path(path_a);
    } else if (strcmp(command, "rename") == 0) {
        rename_path(path_a, path_b);
    } else if (strcmp(command, "sync") == 0) {
        int sync_result = sync_filesystem(argv[first_argument + 1],
                                           argv[first_argument + 2],
                                           argv[first_argument + 3]);
        if (sync_result != EXIT_OK)
            return sync_result;
    } else if (strcmp(command, "list") == 0) {
        list_directory(path_a);
    }

    int result = finish_success();
    if (result != EXIT_OK)
        return result;
    return EXIT_OK;
}
