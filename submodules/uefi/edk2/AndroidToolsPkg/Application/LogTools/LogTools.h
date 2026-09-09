/** @file
 *  Read-only probes of the platform UART log rings for Canoe.
 *
 *  Every collector treats firmware-provided addresses as untrusted and first
 *  proves that the range is present in the current UEFI memory map.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __LOG_TOOLS_H__
#define __LOG_TOOLS_H__

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include <AndroidToolsUi.h>

#define LT_EXPECTED_RING_ADDRESS ((EFI_PHYSICAL_ADDRESS)0x81CE4000ULL)
#define LT_EXPECTED_RING_LENGTH  0x10000U
#define LT_INFO_BLOCK_SIZE       0x48U
#define LT_MAX_RING_LENGTH       (1024U * 1024U)
#define LT_MAX_HOBS              1024U
#define LT_MAX_PHYSICAL_READ     (1024U * 1024U)
#define LT_REPORT_ROWS           64U

#define LT_DUMP_PATH  L"\\canoe\\logprobe.txt"

typedef struct {
  BOOLEAN                 Found;
  BOOLEAN                 Readable;
  EFI_STATUS              Status;
  EFI_PHYSICAL_ADDRESS   Address;
  UINT32                  Signature;
  UINT32                  StructVersion;
  UINT64                  UartLogBufferPtr;
  UINT64                  UartLogBufferLen;
} LT_INFO_BLOCK;

EFI_STATUS
LtCopyPhysical (
  IN EFI_PHYSICAL_ADDRESS Address,
  OUT VOID                *Buffer,
  IN UINTN                 Length
  );

EFI_STATUS
LtReadGuidHobPointer (
  IN  CONST EFI_GUID *Name,
  OUT VOID          **Pointer
  );

EFI_STATUS
LtReadInfoBlock (
  OUT LT_INFO_BLOCK *Info
  );

EFI_STATUS
LtBuildInfoReport (
  OUT AT_REPORT *Report
  );

EFI_STATUS
LtBuildRingReport (
  OUT AT_REPORT *Report
  );

EFI_STATUS
LtDumpToLogfs (
  VOID
  );

CONST AT_REPORT_SOURCE *
LtReportSources (
  OUT UINTN *Count
  );

/* Give the drivers a chance to bind the log partition. Required when this tool
   is launched straight from the ABL, where nothing has connected it yet. */
VOID
LtStartFatStack (
  VOID
  );

/* Report writers. A short write reports EFI_DEVICE_ERROR rather than success:
   a truncated report that still parses invites a wrong conclusion. */
EFI_STATUS
LtWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              BufferSize
  );

EFI_STATUS
LtWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  );

EFI_STATUS
LtWriteUnicodeLine (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR16      *Text
  );

#endif /* __LOG_TOOLS_H__ */

