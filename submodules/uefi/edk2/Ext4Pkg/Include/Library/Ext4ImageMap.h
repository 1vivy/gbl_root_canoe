/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef EXT4_IMAGE_MAP_H
#define EXT4_IMAGE_MAP_H
#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
#define EXT4_IMAGE_BYTES (32U * 1024U * 1024U)
#define EXT4_IMAGE_MIN_BYTES (8U * 1024U * 1024U)
#define EXT4_IMAGE_MAX_BYTES (256U * 1024U * 1024U)
#define EXT4_IMAGE_STEP_BYTES EXT4_IMAGE_MIN_BYTES
#define EXT4_IMAGE_MAX_EXTENTS (EXT4_IMAGE_MAX_BYTES / 1024U)
STATIC inline BOOLEAN Ext4ImageSizeValid (UINT64 Bytes) {
  return Bytes >= EXT4_IMAGE_MIN_BYTES && Bytes <= EXT4_IMAGE_MAX_BYTES && Bytes % EXT4_IMAGE_STEP_BYTES == 0;
}
typedef struct {
  UINT64 Logical;
  UINT64 Physical;
  UINT64 Bytes;
} EXT4_IMAGE_RANGE;
typedef struct {
  EFI_BLOCK_IO_PROTOCOL *Parent;
  UINT32 MediaId;
  UINT64 Bytes;
  /* UUID bytes, little-endian inode number and generation. No C padding is
   * included in the 24-byte export identity. */
  UINT8 Identity[24];
  UINTN Count;
  EXT4_IMAGE_RANGE Ranges[EXT4_IMAGE_MAX_EXTENTS];
} EXT4_IMAGE_MAP;
/* File must come from this embedded ext4 driver. Caller frees the returned map.
 * It is session evidence only: release before exporting persist or remounting.
 * No filesystem allocation, repair, or writes are performed here. */
EFI_STATUS Ext4MapImage (EFI_FILE_PROTOCOL *File, EXT4_IMAGE_MAP **Map);
#endif
