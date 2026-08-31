/** @file
 *  Report the live Qualcomm UEFI info-block fields without modifying them.
 *
 *  The collector prints the values even when they differ from the known DTB
 *  layout; that difference is the useful fact and is never papered over.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Library/PrintLib.h>

#include "LogTools.h"

#define LT_INFO_SIGNATURE \
  ((UINT32)('I' | ('B' << 8) | ('l' << 16) | ('k' << 24)))

EFI_STATUS
LtBuildInfoReport (
  OUT AT_REPORT *Report
  )
{
  LT_INFO_BLOCK Info;
  EFI_STATUS    Status;

  if (Report == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  Status = AtReportInit (Report, LT_REPORT_ROWS);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = LtReadInfoBlock (&Info);
  AtReportAdd (Report, L"hob-found=%s", Info.Found ? L"yes" : L"no");
  if (!Info.Found) {
    AtReportAdd (Report, L"info-block-readable=no (status=%r)", Info.Status);
    return EFI_SUCCESS;
  }
  AtReportAdd (Report, L"info-block-readable=%s (status=%r)",
               Info.Readable ? L"yes" : L"no", Info.Status);
  if (!Info.Readable) {
    return EFI_SUCCESS;
  }
  AtReportAdd (Report, L"info-block-address=0x%016lx",
               (UINT64)Info.Address);
  AtReportAdd (Report, L"signature=0x%08x expected='IBlk'=0x%08x (%s)",
               Info.Signature, LT_INFO_SIGNATURE,
               (Info.Signature == LT_INFO_SIGNATURE) ? L"match" : L"MISMATCH");
  AtReportAdd (Report, L"struct-version=0x%08x", Info.StructVersion);
  AtReportAdd (Report, L"uart-log-buffer-ptr=0x%016lx",
               Info.UartLogBufferPtr);
  AtReportAdd (Report, L"pointer-is-0x81CE4000=%s",
               (Info.UartLogBufferPtr == LT_EXPECTED_RING_ADDRESS) ?
               L"yes" : L"no");
  AtReportAdd (Report, L"uart-log-buffer-len=0x%016lx",
               Info.UartLogBufferLen);
  AtReportAdd (Report, L"length-is-0x10000=%s",
               (Info.UartLogBufferLen == LT_EXPECTED_RING_LENGTH) ?
               L"yes" : L"no");
  return EFI_SUCCESS;
}

