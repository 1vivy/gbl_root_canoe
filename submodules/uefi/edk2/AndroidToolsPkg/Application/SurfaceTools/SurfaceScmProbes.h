/** @file
 * Fixed-allowlist SCM readback used by SurfaceTools' confirmed probe.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_SCM_PROBES_H__
#define __SURFACE_SCM_PROBES_H__

#include <Uefi.h>
#include <AndroidToolsUi.h>
#include <Protocol/EFIScm.h>

/** True only for the supported common SCM prefix and SIP syscall slot. */
BOOLEAN
StScmProtocolSupportsSip (
  IN CONST QCOM_SCM_PROTOCOL *Protocol
  );

/**
  Execute the two fixed read-only SIP calls once and append machine-readable
  rows to Report, including the complete returned policy payload as bounded
  hexadecimal chunks.
**/
EFI_STATUS
StCollectScmPolicy (
  IN OUT AT_REPORT *Report
  );

#endif /* __SURFACE_SCM_PROBES_H__ */
