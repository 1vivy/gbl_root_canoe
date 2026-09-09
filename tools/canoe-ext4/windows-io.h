#ifndef CANOE_WINDOWS_IO_H
#define CANOE_WINDOWS_IO_H
#include <windows.h>
#include <ext2fs/ext2fs.h>
extern io_manager canoe_windows_io_manager;
int canoe_windows_pread(HANDLE handle, void *buffer, size_t count, unsigned long long offset);
#endif
