#ifndef __SUPER_FB_FILE_WINDOW_H__
#define __SUPER_FB_FILE_WINDOW_H__

#include <Uefi.h>
#include <Protocol/BlockIo.h>

typedef struct {
  UINT32  RunCount;
  UINT64  VolumeBytes;
  BOOLEAN StampValid;
} SFB_FILE_WINDOW_INFO;

EFI_STATUS
SfbOpenFileWindow (
  IN  EFI_HANDLE           Volume,
  IN  CONST CHAR16         *Path,
  OUT EFI_HANDLE           *WindowHandle,
  OUT SFB_FILE_WINDOW_INFO *Info
  );

EFI_STATUS
SfbCloseFileWindow (
  IN EFI_HANDLE WindowHandle
  );

EFI_BLOCK_IO_PROTOCOL *
SfbFileWindowBlockIo (
  IN EFI_HANDLE WindowHandle
  );

#ifdef SFB_HOST_BUILD

typedef struct {
  UINT64  LogicalBlock;
  UINT64  PhysicalBlock;
  UINT64  BlockCount;
  UINT32  Flags;
} SFB_FILE_WINDOW_TEST_EXTENT;

typedef struct {
  UINT64  PhysicalBlock;
  UINT64  BlockCount;
} SFB_FILE_WINDOW_TEST_RUN;

typedef struct {
  UINT64  PhysicalByte;
  UINT64  ByteCount;
} SFB_FILE_WINDOW_TEST_SEGMENT;

#define SFB_FILE_WINDOW_TEST_UNWRITTEN   0x00000001U
#define SFB_FILE_WINDOW_TEST_INLINE      0x00000002U
#define SFB_FILE_WINDOW_TEST_COMPRESSED  0x00000004U
#define SFB_FILE_WINDOW_TEST_ENCRYPTED   0x00000008U

EFI_STATUS
SfbFileWindowTestBuildRuns (
  IN  CONST SFB_FILE_WINDOW_TEST_EXTENT *Extents,
  IN  UINTN                              ExtentCount,
  IN  UINT64                             FileBlocks,
  OUT SFB_FILE_WINDOW_TEST_RUN          *Runs,
  IN OUT UINTN                           *RunCount
  );

EFI_STATUS
SfbFileWindowTestWriteStatus (
  IN BOOLEAN StampValid,
  IN BOOLEAN ReadOnly
  );

EFI_STATUS
SfbFileWindowTestTranslate (
  IN  CONST SFB_FILE_WINDOW_TEST_RUN *Runs,
  IN  UINTN                          RunCount,
  IN  UINT32                         PartitionBlockSize,
  IN  UINT64                         VolumeBytes,
  IN  UINT64                         Lba,
  IN  UINTN                          NumberOfBlocks,
  OUT SFB_FILE_WINDOW_TEST_SEGMENT  *Segments,
  IN OUT UINTN                       *SegmentCount
  );

VOID
SfbFileWindowTestSha256 (
  IN  CONST VOID *Data,
  IN  UINTN       DataSize,
  OUT UINT8       Digest[32]
  );

EFI_STATUS
SfbFileWindowTestValidateTrailer (
  IN  CONST UINT8                    *FileBytes,
  IN  UINT64                          FileSize,
  IN  CONST SFB_FILE_WINDOW_TEST_RUN *Runs,
  IN  UINTN                           RunCount,
  OUT BOOLEAN                        *StampValid
  );

#endif

#endif
