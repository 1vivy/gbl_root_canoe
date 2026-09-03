/** @file
 *  MdTools - minidump table probe and RAM-only table editor.
 *
 *  Launched directly from the ABL with `fastboot boot MdTools.efi`. The scan
 *  rows are read-only; the edit rows change DDR only and every edit is
 *  wiped by the next reboot, when XBL rebuilds the table.
 *
 *  Expected values below were measured from the 2026-09-02 Sahara captures
 *  (66 regions, three boots, byte-identical layout). They are printed as
 *  expected=/found pairs; a mismatch is a finding, not a failure.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __MD_TOOLS_H__
#define __MD_TOOLS_H__

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include <AndroidToolsUi.h>
#include <MdTable.h>

#define MD_DUMP_PATH  L"\\canoe\\mdtools.txt"

/* 2026-09-02 capture measurements used for expected= lines. */
#define MD_EXPECT_REGIONS     66u
#define MD_EXPECT_UEFI_ADDR   0x81CE4000ULL
#define MD_EXPECT_UEFI_SIZE   0x10000ULL
#define MD_EXPECT_XBL_ADDR    0x81A00000ULL
#define MD_EXPECT_XBL_SIZE    0x4000ULL
#define MD_EXPECT_TZ_ADDR     0xD85FF000ULL
#define MD_EXPECT_TZ_SIZE     0x3F3000ULL

/* Alias entry names appended by the edit row. */
#define MD_ALIAS_UEFI  "UEFILOGR"
#define MD_ALIAS_XBL   "XBLLOGR"

/* Crash-test write value. */
#define MD_CRASH_PATTERN  0xDEADBEEFu

/** Run MdTableScan once and cache the map for the session. **/
EFI_STATUS
MdEnsureScan (
  VOID
  );

/** Session-cached scan result; NULL until the first scan. **/
CONST MD_TABLE_MAP *
MdCachedMap (
  VOID
  );

/* Report builders (MdReport.c). */
EFI_STATUS
MdBuildMapReport (
  OUT AT_REPORT *Report
  );

EFI_STATUS
MdBuildRegionReport (
  OUT AT_REPORT *Report
  );

/* Edit actions (MdEdit.c). Each runs its own confirm/result screens. */
VOID
MdAppendAliasesScreen (
  VOID
  );

VOID
MdClearEncryptionScreen (
  VOID
  );

VOID
MdCrashTestScreen (
  VOID
  );

/* Report sources shared by the menu and the logfs dump (MdTools.c). */
CONST AT_REPORT_SOURCE *
MdReportSources (
  OUT UINTN *Count
  );

/* logfs plumbing (MdFat.c / MdDump.c). */
VOID
MdStartFatStack (
  VOID
  );

EFI_STATUS
MdDumpToLogfs (
  VOID
  );

EFI_STATUS
MdWriteBytes (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST VOID        *Buffer,
  IN UINTN              BufferSize
  );

EFI_STATUS
MdWriteAscii (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR8       *Text
  );

EFI_STATUS
MdOpenLogfsRoot (
  OUT EFI_FILE_PROTOCOL **Root
  );

EFI_STATUS
MdWriteUnicodeLine (
  IN EFI_FILE_PROTOCOL *File,
  IN CONST CHAR16      *Text
  );

#endif /* __MD_TOOLS_H__ */
