/** @file
 *  Crash-safe, append-only evidence files for standalone AndroidTools apps.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __ANDROID_TOOLS_EVIDENCE_H__
#define __ANDROID_TOOLS_EVIDENCE_H__

#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>

#define AT_EVIDENCE_DIR         L"\\canoe"
#define AT_EVIDENCE_PATH_CHARS  80u
#define AT_EVIDENCE_STEM_CHARS  16u

typedef struct {
  EFI_FILE_PROTOCOL *File;
  CHAR16             Path[AT_EVIDENCE_PATH_CHARS];
  CHAR16             Stem[AT_EVIDENCE_STEM_CHARS];
  UINTN              Rows;
  UINTN              Keep;
  BOOLEAN            Open;
} AT_EVIDENCE;

/** Connect the FAT stack once so logfs can publish SimpleFileSystem. */
VOID
AtEvidenceStartFatStack (
  VOID
  );

/**
  Create a unique \\canoe\\<stem>-<tag>-<sequence>.txt attempt file.
  Existing files are never opened for writing.
**/
EFI_STATUS
AtEvidenceOpen (
  IN  CONST CHAR16 *Stem,
  IN  CONST CHAR16 *Tag,
  IN  UINTN         Keep,
  OUT AT_EVIDENCE  *Evidence
  );

/** Append one complete ASCII row; overlong rows are refused, not shortened. */
EFI_STATUS
AtEvidencePrint (
  IN OUT AT_EVIDENCE *Evidence,
  IN     CONST CHAR16 *Format,
  ...
  );

/** Append already-encoded ASCII bytes. */
EFI_STATUS
AtEvidenceWriteAscii (
  IN OUT AT_EVIDENCE *Evidence,
  IN     CONST CHAR8 *Text
  );

/** Require the filesystem driver to push buffered bytes to the device. */
EFI_STATUS
AtEvidenceFlush (
  IN OUT AT_EVIDENCE *Evidence
  );

/** Flush, close, then prune older files with the same stem. */
EFI_STATUS
AtEvidenceClose (
  IN OUT AT_EVIDENCE *Evidence
  );

#endif /* __ANDROID_TOOLS_EVIDENCE_H__ */
