/** @file
 * Bounded, side-effect-free debug-policy layout selection and parsing.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SurfacePolicy.h"

#define ST_POLICY_REV2_ROOT_COUNT_OFFSET    24u
#define ST_POLICY_REV2_SERIAL_COUNT_OFFSET  156u
#define ST_POLICY_REV5_SERIAL_COUNT_OFFSET  220u
#define ST_POLICY_REV5_QC_ROOT_OFFSET       1024u

STATIC UINT32
ReadU32 (
  IN CONST UINT8 *Data,
  IN UINTN        Offset
  )
{
  return (UINT32)Data[Offset] |
         ((UINT32)Data[Offset + 1] << 8) |
         ((UINT32)Data[Offset + 2] << 16) |
         ((UINT32)Data[Offset + 3] << 24);
}

STATIC UINT64
ReadU64 (
  IN CONST UINT8 *Data,
  IN UINTN        Offset
  )
{
  UINT64 Value;
  UINTN Index;

  Value = 0;
  for (Index = 0; Index < sizeof (UINT64); Index++) {
    Value |= (UINT64)Data[Offset + Index] << (Index * 8);
  }
  return Value;
}

BOOLEAN
StIsSupportedScmRevision (
  IN UINT64 Revision
  )
{
  return (BOOLEAN)(Revision == ST_SCM_REVISION_2 ||
                   Revision == ST_SCM_REVISION_5);
}

ST_POLICY_LAYOUT
StPolicyLayoutForScmRevision (
  IN UINT64 Revision
  )
{
  if (Revision == ST_SCM_REVISION_2) {
    return StPolicyLayoutRevision2;
  }
  if (Revision == ST_SCM_REVISION_5) {
    return StPolicyLayoutRevision5;
  }
  return StPolicyLayoutUnknown;
}

UINTN
StPolicyExpectedSize (
  IN ST_POLICY_LAYOUT Layout
  )
{
  if (Layout == StPolicyLayoutRevision2) {
    return ST_POLICY_SIZE_REVISION_2;
  }
  if (Layout == StPolicyLayoutRevision5) {
    return ST_POLICY_SIZE_REVISION_5;
  }
  return 0;
}

UINT32
StPolicyExpectedRevision (
  IN ST_POLICY_LAYOUT Layout
  )
{
  if (Layout == StPolicyLayoutRevision2) {
    return ST_POLICY_REVISION_2;
  }
  if (Layout == StPolicyLayoutRevision5) {
    return ST_POLICY_REVISION_5;
  }
  return 0;
}

EFI_STATUS
StHexEncode (
  IN  CONST UINT8 *Data,
  IN  UINTN        DataSize,
  OUT CHAR16      *Text,
  IN  UINTN        TextChars
  )
{
  STATIC CONST CHAR16 Hex[] = L"0123456789abcdef";
  UINTN Index;

  if (Text == NULL || (Data == NULL && DataSize != 0)) {
    return EFI_INVALID_PARAMETER;
  }
  if (DataSize > (MAX_UINTN - 1) / 2 ||
      TextChars < DataSize * 2 + 1) {
    return EFI_BUFFER_TOO_SMALL;
  }

  for (Index = 0; Index < DataSize; Index++) {
    Text[Index * 2] = Hex[Data[Index] >> 4];
    Text[Index * 2 + 1] = Hex[Data[Index] & 0x0f];
  }
  Text[DataSize * 2] = L'\0';
  return EFI_SUCCESS;
}

EFI_STATUS
StParsePolicy (
  IN  CONST VOID     *Data,
  IN  UINTN           DataSize,
  IN  UINT64          ScmRevision,
  OUT ST_POLICY_VIEW *View
  )
{
  CONST UINT8 *Bytes;
  ST_POLICY_LAYOUT Layout;
  UINTN ExpectedSize;
  UINT32 ExpectedRevision;
  UINT64 ReservedMask;

  if (Data == NULL || View == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  View->Valid = FALSE;
  View->Layout = StPolicyLayoutUnknown;
  View->ScmRevision = ScmRevision;
  View->Magic = 0;
  View->Size = 0;
  View->Revision = 0;
  View->Flags = 0;
  View->ImageIdBitmap = 0;
  View->RootCount = 0;
  View->SerialCount = 0;
  View->QcRootCount = 0;
  View->ReservedFlags = 0;
  View->OemFlags = 0;

  Layout = StPolicyLayoutForScmRevision (ScmRevision);
  ExpectedSize = StPolicyExpectedSize (Layout);
  ExpectedRevision = StPolicyExpectedRevision (Layout);
  if (Layout == StPolicyLayoutUnknown) {
    return EFI_UNSUPPORTED;
  }
  if (DataSize != ExpectedSize) {
    return EFI_COMPROMISED_DATA;
  }

  Bytes = (CONST UINT8 *)Data;
  View->Layout = Layout;
  View->Magic = ReadU32 (Bytes, 0);
  View->Size = ReadU32 (Bytes, 4);
  View->Revision = ReadU32 (Bytes, 8);
  View->Flags = ReadU64 (Bytes, 12);
  View->ImageIdBitmap = ReadU32 (Bytes, 20);
  View->RootCount = ReadU32 (Bytes, ST_POLICY_REV2_ROOT_COUNT_OFFSET);
  View->SerialCount = ReadU32 (
      Bytes,
      (Layout == StPolicyLayoutRevision2) ?
      ST_POLICY_REV2_SERIAL_COUNT_OFFSET : ST_POLICY_REV5_SERIAL_COUNT_OFFSET);
  View->QcRootCount = (Layout == StPolicyLayoutRevision5) ?
                      ReadU32 (Bytes, ST_POLICY_REV5_QC_ROOT_OFFSET) : 0;
  View->ReservedFlags = (UINT16)((View->Flags >> 32) & 0xffffU);
  View->OemFlags = (UINT16)(View->Flags >> 48);

  if (View->Size != ExpectedSize || View->Revision != ExpectedRevision ||
      View->RootCount > ST_POLICY_MAX_ROOTS ||
      View->SerialCount > ST_POLICY_MAX_SERIALS ||
      View->QcRootCount > ST_POLICY_MAX_ROOTS) {
    return EFI_COMPROMISED_DATA;
  }

  if (Layout == StPolicyLayoutRevision2) {
    ReservedMask = 0x0000FFFFFFFFFFF0ULL;
  } else {
    /* Bit 31 is WLAN in TzDiag but reserved in the public SCM header. Keep
     * it raw and uninterpreted instead of rejecting either target schema. */
    ReservedMask = (0x7fffULL << 9) | (0xffffULL << 32);
  }
  if ((View->Flags & ReservedMask) != 0) {
    return EFI_COMPROMISED_DATA;
  }

  View->Valid = TRUE;
  return EFI_SUCCESS;
}

VOID
StDecodeSecurePredicates (
  IN  UINT64                Status0,
  IN  UINT64                Status1,
  OUT ST_SECURE_PREDICATES *Predicates
  )
{
  (VOID)Status1;
  if (Predicates == NULL) {
    return;
  }
  Predicates->Valid = TRUE;
  Predicates->Production = (BOOLEAN)((Status0 & 0x47U) == 0x40U);
  Predicates->DebugDisabled = (BOOLEAN)((Status0 & (1ULL << 2)) == 0);
  Predicates->ImageCertDebugDisabled = (BOOLEAN)((Status0 & (1ULL << 6)) != 0);
  Predicates->SecureDevice = (BOOLEAN)((Status0 & ((1ULL << 0) |
                                                    (1ULL << 8) |
                                                    (1ULL << 10))) == 0);
}
