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

#ifndef _WIN32
#include <sys/file.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <io.h>
#include <process.h>
#define close _close
#define fsync _commit
#define getpid _getpid
#define O_RDONLY _O_RDONLY
#define O_RDWR _O_RDWR
#define O_BINARY _O_BINARY
#endif

#include <et/com_err.h>
#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>

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

static int source_fd = -1;
static ext2_filsys fs;
static bool mutating;
static const char *source_name;

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

static void fail(int code, const char *message) {
    fprintf(stderr, "canoe-ext4: %s\n", message);
    close_quietly();
    exit(code);
}

static void fail_rc(int code, const char *operation, errcode_t rc) {
    print_rc(operation, rc);
    close_quietly();
    exit(code);
}

static int host_pread(int fd, void *buf, size_t count, off_t offset) {
#ifdef _WIN32
    __int64 old = _lseeki64(fd, 0, SEEK_CUR);
    if (old < 0 || _lseeki64(fd, offset, SEEK_SET) < 0)
        return -1;
    int got = _read(fd, buf, (unsigned int)count);
    int saved = errno;
    (void)_lseeki64(fd, old, SEEK_SET);
    errno = saved;
    return got;
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
    return flock(fd, LOCK_EX | LOCK_NB);
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
        if (missing_ok)
            return 0;
        fail_rc(EXIT_NOT_FOUND, "lookup", rc);
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
    pid_t child = fork();
    if (child < 0)
        fail(EXIT_IO, "cannot start journal recovery");
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        if (force)
            execlp("e2fsck", "e2fsck", "-fy", path, (char *)NULL);
        else
            execlp("e2fsck", "e2fsck", "-p", path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0)
        fail(EXIT_IO, "cannot wait for journal recovery");
    if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 1))
        fail(EXIT_IO, "journal recovery failed");
    fprintf(stderr, "journal_recovery=completed\n");
}
#else
static void recover_journal(const char *path, bool force) {
    (void)path;
    if (force)
        fail(EXIT_UNSUPPORTED, "journal recovery is unavailable in this Windows build");
}
#endif

static void open_filesystem(bool writable, bool recover) {
    struct ext2_super_block raw_super;
    read_superblock(&raw_super);
    check_supported(&raw_super);
    if (writable && (raw_super.s_feature_ro_compat & EXT4_FEATURE_RO_COMPAT_READONLY) != 0)
        fail(EXIT_UNSUPPORTED, "filesystem is marked read-only by its feature flags");
    if (writable && is_dirty(&raw_super) && !recover)
        fail(EXIT_DIRTY, "filesystem is dirty; retry with --recover");
    if (writable)
        recover_journal(source_name, recover || is_dirty(&raw_super));

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
            rc = ext2fs_mkdir2(fs, current, 0, 0755, 0, component, &next);
            if (rc != 0)
                fail_rc(EXIT_IO, "mkdir", rc);
            current = next;
        }
        component = strtok_r(NULL, "/", &save);
    }
    return current;
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
    } else {
        rc = ext2fs_new_inode(fs, parent, LINUX_S_IFREG | 0644, fs->inode_map, &inode);
        if (rc != 0)
            fail_rc(EXIT_IO, "new-inode", rc);
        ext2fs_inode_alloc_stats2(fs, inode, 1, 0);
        struct ext2_inode metadata;
        memset(&metadata, 0, sizeof(metadata));
        metadata.i_mode = LINUX_S_IFREG | 0644;
        metadata.i_links_count = 1;
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

struct freed_blocks {
    unsigned int count;
    dgrp_t last_group;
};

static int free_block(ext2_filsys volume, blk64_t *block, e2_blkcnt_t block_count,
                      blk64_t ref_block, int ref_offset, void *priv_data) {
    (void)block_count;
    (void)ref_block;
    (void)ref_offset;
    struct freed_blocks *freed = priv_data;
    if (*block != 0) {
        freed->last_group = ext2fs_group_of_blk2(volume, *block);
        ++freed->count;
        ext2fs_block_alloc_stats2(volume, *block, 0);
    }
    *block = 0;
    return BLOCK_CHANGED;
}

static void release_inode(ext2_ino_t inode, struct ext2_inode *metadata) {
    struct freed_blocks freed = {0};
    errcode_t rc;
    if ((metadata->i_flags & EXT4_INLINE_DATA_FL) == 0) {
        rc = ext2fs_block_iterate3(fs, inode, BLOCK_FLAG_DEPTH_TRAVERSE, NULL,
                                   free_block, &freed);
        if (rc != 0)
            fail_rc(EXIT_IO, "free-blocks", rc);
    }
    bool is_directory = LINUX_S_ISDIR(metadata->i_mode);
    metadata->i_links_count = 0;
    metadata->i_dtime = (uint32_t)time(NULL);
    rc = ext2fs_write_inode(fs, inode, metadata);
    if (rc != 0)
        fail_rc(EXIT_IO, "write-deleted-inode", rc);
    ext2fs_inode_alloc_stats2(fs, inode, 0, 0);
    if (freed.count != 0) {
        ext2fs_free_blocks_count_add(fs->super, freed.count);
        ext2fs_bg_free_blocks_count_set(
            fs, freed.last_group,
            ext2fs_bg_free_blocks_count(fs, freed.last_group) + freed.count);
    }
    ++fs->super->s_free_inodes_count;
    dgrp_t group = ext2fs_group_of_ino(fs, inode);
    ext2fs_bg_free_inodes_count_set(
        fs, group, ext2fs_bg_free_inodes_count(fs, group) + 1);
    if (is_directory) {
        __u32 used = ext2fs_bg_used_dirs_count(fs, group);
        if (used > 0)
            ext2fs_bg_used_dirs_count_set(fs, group, used - 1);
    }
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
    uint64_t total_blocks = ext2fs_blocks_count(super);
    uint64_t block_size = fs->blocksize;
    printf("{\"state\":\"");
    fputs(is_dirty(super) ? "dirty" : "clean", stdout);
    printf("\",\"total_blocks\":%" PRIu64 ",\"total_bytes\":%" PRIu64
           ",\"free_blocks\":%" PRIu64 ",\"free_bytes\":%" PRIu64,
           total_blocks, total_blocks * block_size, free_blocks,
           free_blocks * block_size);
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

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--recover] [--mkdir-p] [--path PATH] COMMAND ...\n"
            "commands:\n"
            "  inspect SOURCE [--path PATH]\n"
            "  read SOURCE PATH\n"
            "  write SOURCE PATH < STDIN\n"
            "  mkdir SOURCE PATH\n"
            "  remove SOURCE PATH\n"
            "  rename SOURCE OLD_PATH NEW_PATH\n"
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
    options_t options = parse_options(argc, argv);
    const char *command = argv[options.command_index];
    int first_argument = options.command_index + 1;
    int argument_count = argc - first_argument;
    bool command_mutates = strcmp(command, "write") == 0 || strcmp(command, "mkdir") == 0 ||
                           strcmp(command, "remove") == 0 || strcmp(command, "rename") == 0;
    if (strcmp(command, "inspect") != 0 && strcmp(command, "read") != 0 &&
        strcmp(command, "write") != 0 && strcmp(command, "mkdir") != 0 &&
        strcmp(command, "remove") != 0 && strcmp(command, "rename") != 0 &&
        strcmp(command, "list") != 0)
        usage(argv[0]);

    bool trailing_inspect_path = strcmp(command, "inspect") == 0 && argument_count == 3 &&
                                 strcmp(argv[first_argument + 1], "--path") == 0;
    if (trailing_inspect_path)
        options.inspect_path = argv[first_argument + 2];
    int required = strcmp(command, "rename") == 0 ? 3 : (strcmp(command, "inspect") == 0 ? 1 : 2);
    if (trailing_inspect_path)
        argument_count = 1;
    if (argument_count != required)
        usage(argv[0]);
    if (options.inspect_path != NULL && strcmp(command, "inspect") != 0)
        usage(argv[0]);
    if (options.mkdir_p && strcmp(command, "write") != 0 && strcmp(command, "mkdir") != 0)
        usage(argv[0]);

    const char *source = argv[first_argument];
    struct stat source_stat;
    if (stat(source, &source_stat) < 0)
        fail(EXIT_IO, "cannot stat source");
    int open_flags = (command_mutates ? O_RDWR : O_RDONLY);
#ifdef _WIN32
    open_flags |= O_BINARY;
#endif
    source_fd = open(source, open_flags);
    if (source_fd < 0)
        fail(EXIT_IO, "cannot open source");
    if (lock_source(source_fd) < 0)
        fail(EXIT_IO, "cannot acquire exclusive source lock");
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
    if (strcmp(command, "inspect") == 0) {
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
    open_filesystem(command_mutates, options.recover);
    if (strcmp(command, "inspect") == 0) {
        inspect_filesystem(path_a);
    } else if (strcmp(command, "read") == 0) {
        read_file_to_stdout(path_a);
        if (fflush(stdout) != 0)
            fail(EXIT_IO, "stdout flush failed");
    } else if (strcmp(command, "write") == 0) {
        unsigned char *data = NULL;
        size_t length = 0;
        read_stdin(&data, &length);
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
            errcode_t rc = ext2fs_mkdir2(fs, parent_inode, 0, 0755, 0, name, NULL);
            if (rc != 0)
                fail_rc(EXIT_IO, "mkdir", rc);
        }
    } else if (strcmp(command, "remove") == 0) {
        remove_path(path_a);
    } else if (strcmp(command, "rename") == 0) {
        rename_path(path_a, path_b);
    } else if (strcmp(command, "list") == 0) {
        list_directory(path_a);
    }

    int result = finish_success();
    if (result != EXIT_OK)
        return result;
    return EXIT_OK;
}
