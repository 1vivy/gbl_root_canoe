/** @file
 * Bounded, side-effect-free debug-policy layout selection and parsing.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __SURFACE_POLICY_H__
#define __SURFACE_POLICY_H__

#include <Uefi.h>

#define ST_SCM_REVISION_2              0x0000000000040001ULL
#define ST_SCM_REVISION_5              0x0000000000050002ULL
#define ST_SCM_SECURE_STATE_ID         0x02000604U
#define ST_SCM_SECURE_STATE_PARAM_ID   0x00000000U
#define ST_SCM_POLICY_ID               0x02001401U
#define ST_SCM_POLICY_PARAM_ID         0x00000022U

#define ST_POLICY_SIZE_REVISION_2      960u
#define ST_POLICY_SIZE_REVISION_5      1220u
#define ST_POLICY_REVISION_2           2u
#define ST_POLICY_REVISION_5           5u
#define ST_POLICY_MAX_ROOTS            4u
#define ST_POLICY_MAX_SERIALS          200u

#define ST_POLICY_FLAG_ONLINE          0u
#define ST_POLICY_FLAG_OFFLINE         1u
#define ST_POLICY_FLAG_JTAG            2u
#define ST_POLICY_FLAG_LOGS            3u
#define ST_POLICY_FLAG_MODEM_INV       4u
#define ST_POLICY_FLAG_MODEM_NINV      5u
#define ST_POLICY_FLAG_APPS_INV        6u
#define ST_POLICY_FLAG_DEBUG_LEVEL0    7u
#define ST_POLICY_FLAG_DEBUG_LEVEL1    8u
#define ST_POLICY_FLAG_NONSECURE_DUMP  24u
#define ST_POLICY_FLAG_ENCRYPTED_APPS  25u
#define ST_POLICY_FLAG_ENCRYPTED_MPSS  26u
#define ST_POLICY_FLAG_ENCRYPTED_LPASS 27u
#define ST_POLICY_FLAG_ENCRYPTED_CSS   28u
#define ST_POLICY_FLAG_ENCRYPTED_ADSP  29u
#define ST_POLICY_FLAG_ENCRYPTED_CDSP  30u
#define ST_POLICY_FLAG_SCHEMA_CONFLICT 31u

typedef enum {
  StPolicyLayoutUnknown = 0,
  StPolicyLayoutRevision2,
  StPolicyLayoutRevision5
} ST_POLICY_LAYOUT;

typedef struct {
  BOOLEAN          Valid;
  ST_POLICY_LAYOUT Layout;
  UINT64           ScmRevision;
  UINT32           Magic;
  UINT32           Size;
  UINT32           Revision;
  UINT64           Flags;
  UINT32           ImageIdBitmap;
  UINT32           RootCount;
  UINT32           SerialCount;
  UINT32           QcRootCount;
  UINT16           ReservedFlags;
  UINT16           OemFlags;
} ST_POLICY_VIEW;

typedef struct {
  BOOLEAN Valid;
  BOOLEAN Production;
  BOOLEAN DebugDisabled;
  BOOLEAN ImageCertDebugDisabled;
  BOOLEAN SecureDevice;
} ST_SECURE_PREDICATES;

BOOLEAN
StIsSupportedScmRevision (
  IN UINT64 Revision
  );

ST_POLICY_LAYOUT
StPolicyLayoutForScmRevision (
  IN UINT64 Revision
  );

UINTN
StPolicyExpectedSize (
  IN ST_POLICY_LAYOUT Layout
  );

UINT32
StPolicyExpectedRevision (
  IN ST_POLICY_LAYOUT Layout
  );

EFI_STATUS
StHexEncode (
  IN  CONST UINT8 *Data,
  IN  UINTN        DataSize,
  OUT CHAR16      *Text,
  IN  UINTN        TextChars
  );

EFI_STATUS
StParsePolicy (
  IN  CONST VOID       *Data,
  IN  UINTN             DataSize,
  IN  UINT64            ScmRevision,
  OUT ST_POLICY_VIEW   *View
  );

VOID
StDecodeSecurePredicates (
  IN  UINT64                  Status0,
  IN  UINT64                  Status1,
  OUT ST_SECURE_PREDICATES    *Predicates
  );

#endif /* __SURFACE_POLICY_H__ */
