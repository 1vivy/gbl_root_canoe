/** @file
 *  MdTools - bounded minidump report and owned-subsystem crash probe.
 *
 *  Launched directly from the ABL with `fastboot boot MdTools.efi`. Discovery
 *  resolves SMEM item 602 and reads only its fixed global ToC plus the BOOT and
 *  AOP arrays those live slots name. Before discovery begins, the selected
 *  pathway creates and flushes a durable stage marker. A crash therefore
 *  leaves the exact pending stage in logfs instead of an empty partition.
 *
 *  The mutation pathway claims one free subsystem slot, pointing it at this
 *  image's own one-entry array, then deliberately triggers collection while
 *  the image remains resident. Every store is preceded by a flushed intent
 *  row and followed by a flushed read-back outcome. XBL rebuilds the table on
 *  every boot, so no edit survives reset.
 *
 *  Expected values below were measured from the 2026-09-02 Sahara captures
 *  and the 2026-09-29 qdl capture. They are printed as expected=/found pairs;
 *  a mismatch is a finding, not a failure.
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

/* 2026-09-02 capture measurements used for expected= lines. */
#define MD_EXPECT_REGIONS     66u
#define MD_EXPECT_UEFI_ADDR   0x81CE4000ULL
#define MD_EXPECT_UEFI_SIZE   0x10000ULL
#define MD_EXPECT_XBL_ADDR    0x81A00000ULL
#define MD_EXPECT_XBL_SIZE    0x4000ULL
#define MD_EXPECT_TZ_ADDR     0xD820C000ULL
#define MD_EXPECT_TZ_SIZE     0x3F3000ULL

#define MD_CRASH_PATTERN      0xDEADBEEFu

/* Our own probe buffer and region. Keep the name out of the collector's
   filtered ADSP/CDSP/MSS families or the shipped collector will drop it. */
#define MD_PROBE_BUFFER_SIZE  1024u
#define MD_PROBE_REGION_NAME  "CANOEPROBE"
#define MD_PROBE_BANNER       "CANOE-BDS-PLAINTEXT-PROBE"

typedef struct MD_EVIDENCE MD_EVIDENCE;

/** Run bounded discovery once, recording and flushing each stage. **/
EFI_STATUS
MdEnsureScan (
  IN OUT MD_EVIDENCE *Evidence
  );

/** Session-cached scan result; NULL until the first scan. **/
CONST MD_TABLE_MAP *
MdCachedMap (
  VOID
  );

/**
  Name one raw ToC word when it is one of the fourcc magics the vendor writes,
  so no reader has to decode hex by eye. Unknown values return L"(----)".
**/
CONST CHAR16 *
MdFieldFourcc (
  IN UINT32 Value
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

/* Report sources shared by the menu and the evidence writer (MdTools.c). */
CONST AT_REPORT_SOURCE *
MdReportSources (
  OUT UINTN *Count
  );

/*
 * Durable evidence (MdDump.c).
 *
 * One file per rung attempt, named md-<tag>-<seq>.txt under \canoe, where tag
 * identifies the pathway and rung (for example `own2`); seq is one above the
 * highest sequence already present. A new attempt therefore never overwrites an
 * earlier one; the oldest files are pruned only after a later attempt has been
 * written, flushed and closed, and only down to MD_EVIDENCE_KEEP.
 */
#define MD_EVIDENCE_DIR        L"\\canoe"
#define MD_EVIDENCE_KEEP       4u
#define MD_EVIDENCE_PATH_CHARS 64u

struct MD_EVIDENCE {
  EFI_FILE_PROTOCOL *File;
  CHAR16             Path[MD_EVIDENCE_PATH_CHARS];
  UINTN              Rows;
  BOOLEAN            Open;
};

/**
  Create this attempt's evidence file. Fails rather than reuse a name that
  already exists, so no durable record is destroyed by starting a new one.
**/
EFI_STATUS
MdEvidenceOpen (
  IN  CONST CHAR16 *Tag,
  OUT MD_EVIDENCE  *Evidence
  );

/**
  Append one ASCII row. Returns EFI_BAD_BUFFER_SIZE instead of writing a row
  that would not fit AT_ROW_CHARS - a silently shortened measurement is worse
  than a failed rung.
**/
EFI_STATUS
MdEvidencePrint (
  IN OUT MD_EVIDENCE *Evidence,
  IN     CONST CHAR16 *Format,
  ...
  );

/** Append every report section (same sections the menu shows). **/
EFI_STATUS
MdEvidenceWriteReports (
  IN OUT MD_EVIDENCE *Evidence
  );

/** Push buffered bytes to the partition. EFI_UNSUPPORTED when no Flush. **/
EFI_STATUS
MdEvidenceFlush (
  IN OUT MD_EVIDENCE *Evidence
  );

/** Flush, close, then prune older attempts. **/
EFI_STATUS
MdEvidenceClose (
  IN OUT MD_EVIDENCE *Evidence
  );

/*
 * Gate and walker (MdEdit.c).
 *
 * Builds the rung's intent rows. Return an error to REFUSE the rung: nothing
 * is written and the refusal is recorded.
 */
typedef EFI_STATUS (*MD_INTENT_FN) (
  IN OUT MD_EVIDENCE *Evidence
  );

/*
 * Rung body. Called only after the intent record has been flushed. Performs
 * the writes, reads every written field back, and appends its own outcome
 * rows; the gate flushes those before closing.
 */
typedef EFI_STATUS (*MD_RUNG_FN) (
  IN OUT MD_EVIDENCE *Evidence
  );

/**
  Evidence, then outcome - for a rung that mutates nothing (rung 1 of every
  pathway). Step is the human label, Tag the file-name token.
**/
EFI_STATUS
MdRecord (
  IN CONST CHAR16 *Step,
  IN CONST CHAR16 *Tag,
  IN MD_INTENT_FN  Intent OPTIONAL
  );

/**
  Evidence, flush, ACT, outcome, flush. Aborts the action - performs no write
  at all - when the evidence record or its flush fails, because a mutation
  with no durable record of it is pure risk for zero information.
**/
EFI_STATUS
MdAct (
  IN CONST CHAR16 *Step,
  IN CONST CHAR16 *Tag,
  IN MD_INTENT_FN  Intent OPTIONAL,
  IN MD_RUNG_FN    Act
  );

typedef struct {
  CONST CHAR16 *Name;
  CONST CHAR16 *Tag;
  MD_INTENT_FN  Intent;
  MD_RUNG_FN    Act;      /* NULL: a record-only rung */
} MD_RUNG;

typedef struct {
  CONST CHAR16  *Name;
  CONST MD_RUNG *Rungs;
  UINTN          RungCount;
  /* Also page the built reports on the console when the walk completes. Used
     by the report pathway, which is what the menu's old read-only view rows
     became. */
  BOOLEAN        ShowReports;
  /* TRUE when this pathway registers a minidump region pointing at this
     image's own memory. Such a registration is only meaningful while MdTools
     is resident, so the pathway must not return to the menu as though it had
     succeeded: it ends in a terminal trigger, and surviving that trigger means
     nothing was collected. */
  BOOLEAN        Registers;
} MD_PATHWAY;

/** The pathway table in menu order. **/
CONST MD_PATHWAY *
MdPathways (
  OUT UINTN *Count
  );

/**
  Walk one pathway's rungs in order, stopping at the first rung that refuses or
  fails. Returns EFI_SUCCESS only when every rung completed and verified.
**/
EFI_STATUS
MdWalkPathway (
  IN  CONST MD_PATHWAY *Pathway,
  OUT UINTN            *CompletedRungs OPTIONAL
  );

/* logfs plumbing (MdFat.c / MdDump.c). */
VOID
MdStartFatStack (
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
