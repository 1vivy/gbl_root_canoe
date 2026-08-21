#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "patchs/core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#endif

static int read_file(const char *filename, char **data, int32_t *size) {
    FILE *file = fopen(filename, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long length = ftell(file);
    if (length <= 0 || length > INT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    *size = (int32_t)length;
    *data = malloc((size_t)*size);
    if (!*data) {
        fclose(file);
        return -1;
    }
    size_t bytes_read = fread(*data, 1, (size_t)*size, file);
    int close_status = fclose(file);
    if (bytes_read != (size_t)*size || close_status != 0) {
        free(*data);
        *data = NULL;
        return -1;
    }
    return 0;
}

static unsigned long process_id(void) {
#ifdef _WIN32
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

static int sync_output(FILE *file) {
    if (fflush(file) != 0) return -1;
#ifdef _WIN32
    return _commit(_fileno(file));
#else
    return fsync(fileno(file));
#endif
}

static int replace_output(const char *temporary, const char *output) {
#ifdef _WIN32
    return MoveFileExA(temporary, output,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? 0
               : -1;
#else
    return rename(temporary, output);
#endif
}

#define ATOMIC_TEMP_ATTEMPTS 100u

static int open_temporary(const char *filename) {
#ifdef _WIN32
    return _open(filename, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
#else
    return open(filename, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
}

static int close_temporary_fd(int fd) {
#ifdef _WIN32
    return _close(fd);
#else
    return close(fd);
#endif
}

static FILE *fdopen_temporary(int fd) {
#ifdef _WIN32
    return _fdopen(fd, "wb");
#else
    return fdopen(fd, "wb");
#endif
}

static int write_file_atomic(const char *filename, const char *data,
                             int32_t size) {
    size_t filename_length = strlen(filename);
    if (filename_length > SIZE_MAX - 64u) return -1;

    size_t temporary_bytes = filename_length + 64u;
    char *temporary = malloc(temporary_bytes);
    if (temporary == NULL) return -1;

    FILE *out = NULL;
    unsigned long pid = process_id();
    for (unsigned int attempt = 0; attempt < ATOMIC_TEMP_ATTEMPTS; ++attempt) {
        int path_length = snprintf(temporary, temporary_bytes, "%s.tmp.%lu.%u",
                                   filename, pid, attempt);
        if (path_length < 0 || (size_t)path_length >= temporary_bytes) {
            break;
        }
        int fd = open_temporary(temporary);
        if (fd >= 0) {
            out = fdopen_temporary(fd);
            if (out == NULL) {
                close_temporary_fd(fd);
                remove(temporary);
            }
            break;
        }
        if (errno != EEXIST) {
            break;
        }
    }
    if (out == NULL) {
        free(temporary);
        return -1;
    }

    size_t bytes_written = fwrite(data, 1, (size_t)size, out);
    if (bytes_written != (size_t)size || sync_output(out) != 0) {
        fclose(out);
        remove(temporary);
        free(temporary);
        return -1;
    }
    if (fclose(out) != 0 || replace_output(temporary, filename) != 0) {
        remove(temporary);
        free(temporary);
        return -1;
    }
    free(temporary);
    return 0;
}


int32_t main(int32_t argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <input_file> <output_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    char* data = NULL;
    int32_t size = 0;
    if (read_file(argv[1], &data, &size) != 0) {
        printf("Failed to read file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    if (!PatchBuffer(data,size))
    {
        printf("Patching failed\n");
        free(data);
        return EXIT_FAILURE;
    }
    if (write_file_atomic(argv[2], data, size) != 0) {
        printf("Failed to write output: %s\n", argv[2]);
        free(data);
        return EXIT_FAILURE;
    }
    free(data);
    printf("Saved to %s\n", argv[2]);
    return EXIT_SUCCESS;
}