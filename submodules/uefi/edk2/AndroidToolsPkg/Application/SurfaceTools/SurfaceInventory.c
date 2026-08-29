/** @file
 *  Passive protocol and configuration-table inventory for SurfaceTools.
 *
 *  This file never invokes a protocol method. It only asks Boot Services for
 *  handle metadata and inspects documented interface fields.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/DebugImageInfoTable.h>
#include <Guid/DxeServices.h>
#include <Guid/HobList.h>
#include <Guid/SmBios.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/Cpu.h>
#include <Protocol/DebugSupport.h>
#include <Protocol/EFIQseecom.h>
#include <Protocol/EFIScm.h>
#include <Protocol/EFISPSS.h>
#include <Protocol/EFIUsbDevice.h>
#include <Protocol/EFIUsbfnIo.h>
#include <Protocol/EFIVerifiedBoot.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Security.h>
#include <Protocol/Security2.h>
#include <Protocol/SimpleFileSystem.h>

#include "SurfaceInventory.h"

CONST AT_REPORT_SOURCE gStPassiveReports[ST_PASSIVE_REPORT_COUNT] = {
  { L"Execution Summary",     StBuildSummaryReport },
  { L"Known Policy Surfaces", StBuildPolicyReport },
  { L"Protocol GUID Census",  StBuildProtocolReport },
  { L"Configuration Tables",  StBuildTableReport },
  { L"Loaded Images",         StBuildImageReport },
  { L"Memory Map",            StBuildMemoryReport },
};

#define ST_MAX_PROTOCOLS  512u
#define ST_MAX_TABLES     128u

VOID
StFormatGuid (
  IN  CONST EFI_GUID *Guid,
  OUT CHAR16         *Buffer,
  IN  UINTN          BufferChars
  )
{
  if (Buffer == NULL || BufferChars == 0) {
    return;
  }
  Buffer[0] = L'\0';
  if (Guid == NULL || BufferChars < 37) {
    return;
  }

  UnicodeSPrint (
      Buffer, BufferChars * sizeof (CHAR16),
      L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      Guid->Data1, Guid->Data2, Guid->Data3,
      Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3],
      Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
}

typedef BOOLEAN (*ST_METHOD_CHECK)(IN VOID *Interface);

typedef struct {
  CONST CHAR16   *Name;
  CONST CHAR16   *Class;
  EFI_GUID       *Guid;
  ST_METHOD_CHECK HasMethod;
} ST_KNOWN_PROTOCOL;

typedef struct {
  EFI_GUID Guid;
  UINTN    Handles;
} ST_GUID_COUNT;

typedef struct {
  EFI_GUID Guid;
} ST_TABLE;

STATIC BOOLEAN HasScmQuery (VOID *P) {
  QCOM_SCM_PROTOCOL *S = P;
  return S->Revision == QCOM_SCM_PROTOCOL_REVISION &&
         S->ScmGetVersion != NULL;
}
STATIC BOOLEAN HasQseeDispatch (VOID *P) {
  QCOM_QSEECOM_PROTOCOL *Q = P;
  return Q->Revision == QCOM_QSEECOM_PROTOCOL_REVISION &&
         Q->QseecomStartApp != NULL && Q->QseecomSendCmd != NULL;
}
STATIC BOOLEAN HasVbQueries (VOID *P) {
  QCOM_VERIFIEDBOOT_PROTOCOL *V = P;
  return V->Revision == QCOM_VERIFIEDBOOT_PROTOCOL_REVISION &&
         V->VBIsDeviceSecure != NULL && V->VBGetBootState != NULL &&
         V->VBIsKeymasterEnabled != NULL;
}
STATIC BOOLEAN HasSecurityGate (VOID *P) {
  return ((EFI_SECURITY_ARCH_PROTOCOL *)P)->FileAuthenticationState != NULL;
}
STATIC BOOLEAN HasSecurity2Gate (VOID *P) {
  return ((EFI_SECURITY2_ARCH_PROTOCOL *)P)->FileAuthentication != NULL;
}
STATIC BOOLEAN HasDebugQuery (VOID *P) {
  return ((EFI_DEBUG_SUPPORT_PROTOCOL *)P)->GetMaximumProcessorIndex != NULL;
}
STATIC BOOLEAN HasExceptionHook (VOID *P) {
  return ((EFI_CPU_ARCH_PROTOCOL *)P)->RegisterInterruptHandler != NULL;
}
STATIC BOOLEAN HasBlockRead (VOID *P) {
  return ((EFI_BLOCK_IO_PROTOCOL *)P)->ReadBlocks != NULL;
}
STATIC BOOLEAN HasFileOpen (VOID *P) {
  return ((EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *)P)->OpenVolume != NULL;
}

STATIC ST_KNOWN_PROTOCOL mKnown[] = {
  { L"SCM",          L"secure gateway", &gQcomScmProtocolGuid,             HasScmQuery },
  { L"QSEECom",      L"TEE app gateway", &gQcomQseecomProtocolGuid,        HasQseeDispatch },
  { L"SPSS",         L"secure service", &gEfiSPSSProtocolGuid,             NULL },
  { L"VerifiedBoot", L"boot policy",    &gEfiQcomVerifiedBootProtocolGuid, HasVbQueries },
  { L"Security",     L"image gate",     &gEfiSecurityArchProtocolGuid,     HasSecurityGate },
  { L"Security2",    L"image gate",     &gEfiSecurity2ArchProtocolGuid,    HasSecurity2Gate },
  { L"DebugSupport", L"debug callback", &gEfiDebugSupportProtocolGuid,     HasDebugQuery },
  { L"CpuArch",      L"exception hook", &gEfiCpuArchProtocolGuid,          HasExceptionHook },
  { L"UsbDevice",    L"USB gadget",     &gEfiUsbDeviceProtocolGuid,        NULL },
  { L"UsbFnIo",      L"USB controller", &gEfiUsbfnIoProtocolGuid,          NULL },
  { L"BlockIo",      L"storage",        &gEfiBlockIoProtocolGuid,          HasBlockRead },
  { L"SimpleFS",     L"filesystem",     &gEfiSimpleFileSystemProtocolGuid, HasFileOpen },
  { L"LoadedImage",  L"image metadata", &gEfiLoadedImageProtocolGuid,      NULL },
};

STATIC CONST CHAR16 *
KnownGuidName (CONST EFI_GUID *Guid)
{
  UINTN Index;
  for (Index = 0; Index < sizeof (mKnown) / sizeof (mKnown[0]); Index++) {
    if (CompareGuid (Guid, mKnown[Index].Guid)) {
      return mKnown[Index].Name;
    }
  }
  return L"";
}

STATIC VOID
SortGuidCounts (ST_GUID_COUNT *Items, UINTN Count)
{
  UINTN I;
  UINTN J;
  ST_GUID_COUNT Swap;
  for (I = 1; I < Count; I++) {
    for (J = I; J > 0 && CompareMem (&Items[J - 1].Guid, &Items[J].Guid,
                                     sizeof (EFI_GUID)) > 0; J--) {
      Swap = Items[J - 1];
      Items[J - 1] = Items[J];
      Items[J] = Swap;
    }
  }
}

EFI_STATUS
StBuildPolicyReport (OUT AT_REPORT *Report)
{
  UINTN Index;
  VOID *Interface;
  EFI_STATUS Status;
  ST_PROBE_OBSERVATION Observation;

  Status = AtReportInit (Report, sizeof (mKnown) / sizeof (mKnown[0]) + 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  AtReportAdd (Report, L"EUD: no known UEFI GUID; use external USB/SWD probe");

  for (Index = 0; Index < sizeof (mKnown) / sizeof (mKnown[0]); Index++) {
    Interface = NULL;
    Status = gBS->LocateProtocol (mKnown[Index].Guid, NULL, &Interface);
    Observation.Present = (BOOLEAN)(!EFI_ERROR (Status) && Interface != NULL);
    Observation.MethodPresent = (BOOLEAN)(Observation.Present &&
        mKnown[Index].HasMethod != NULL && mKnown[Index].HasMethod (Interface));
    Observation.Invoked = FALSE;
    Observation.Status = EFI_NOT_READY;
    Observation.EffectObserved = FALSE;
    AtReportAdd (Report, L"%s: %s [%s]", mKnown[Index].Name,
                 StProbeStateName (StClassifyProbe (&Observation)),
                 mKnown[Index].Class);
  }
  return EFI_SUCCESS;
}

EFI_STATUS
StBuildProtocolReport (OUT AT_REPORT *Report)
{
  EFI_HANDLE *Handles;
  EFI_GUID **Guids;
  ST_GUID_COUNT *Items;
  EFI_STATUS Status;
  UINTN HandleCount;
  UINTN GuidCount;
  UINTN Count;
  UINTN H;
  UINTN G;
  UINTN I;
  CHAR16 GuidText[37];

  Status = AtReportInit (Report, ST_MAX_PROTOCOLS);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Items = AllocateZeroPool (ST_MAX_PROTOCOLS * sizeof (*Items));
  if (Items == NULL) {
    AtReportFree (Report);
    return EFI_OUT_OF_RESOURCES;
  }

  Handles = NULL;
  HandleCount = 0;
  Count = 0;
  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL,
                                    &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    AtReportAdd (Report, L"Handle census failed: %r", Status);
    FreePool (Items);
    return EFI_SUCCESS;
  }

  for (H = 0; H < HandleCount; H++) {
    Guids = NULL;
    GuidCount = 0;
    Status = gBS->ProtocolsPerHandle (Handles[H], &Guids, &GuidCount);
    if (EFI_ERROR (Status) || Guids == NULL) {
      continue;
    }
    for (G = 0; G < GuidCount; G++) {
      if (Guids[G] == NULL) {
        continue;
      }
      for (I = 0; I < Count; I++) {
        if (CompareGuid (&Items[I].Guid, Guids[G])) {
          Items[I].Handles++;
          break;
        }
      }
      if (I == Count) {
        if (Count == ST_MAX_PROTOCOLS) {
          Report->Truncated = TRUE;
          continue;
        }
        Items[Count].Guid = *Guids[G];
        Items[Count].Handles = 1;
        Count++;
      }
    }
    FreePool (Guids);
  }
  FreePool (Handles);

  SortGuidCounts (Items, Count);
  for (I = 0; I < Count; I++) {
    StFormatGuid (&Items[I].Guid, GuidText,
                  sizeof (GuidText) / sizeof (GuidText[0]));
    AtReportAdd (Report, L"%s  h=%Lu %s", GuidText,
                 (UINT64)Items[I].Handles, KnownGuidName (&Items[I].Guid));
  }
  FreePool (Items);
  return EFI_SUCCESS;
}

STATIC CONST CHAR16 *
TableName (CONST EFI_GUID *Guid)
{
  if (CompareGuid (Guid, &gEfiAcpi20TableGuid)) return L"ACPI 2";
  if (CompareGuid (Guid, &gEfiAcpi10TableGuid)) return L"ACPI 1";
  if (CompareGuid (Guid, &gEfiSmbios3TableGuid)) return L"SMBIOS 3";
  if (CompareGuid (Guid, &gEfiSmbiosTableGuid)) return L"SMBIOS";
  if (CompareGuid (Guid, &gEfiHobListGuid)) return L"HOB list";
  if (CompareGuid (Guid, &gEfiDxeServicesTableGuid)) return L"DXE services";
  if (CompareGuid (Guid, &gEfiDebugImageInfoTableGuid)) return L"debug images";
  return L"";
}

EFI_STATUS
StBuildTableReport (OUT AT_REPORT *Report)
{
  ST_TABLE *Tables;
  ST_TABLE Swap;
  EFI_STATUS Status;
  UINTN Count;
  UINTN I;
  UINTN J;
  CHAR16 GuidText[37];

  Count = gST->NumberOfTableEntries;
  Status = AtReportInit (Report, (Count == 0) ? 1 :
                        ((Count < ST_MAX_TABLES) ? Count : ST_MAX_TABLES));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Count == 0) {
    AtReportAdd (Report, L"No configuration tables");
    return EFI_SUCCESS;
  }
  if (Count > ST_MAX_TABLES) {
    Count = ST_MAX_TABLES;
    Report->Truncated = TRUE;
  }

  Tables = AllocateZeroPool (Count * sizeof (*Tables));
  if (Tables == NULL) {
    AtReportFree (Report);
    return EFI_OUT_OF_RESOURCES;
  }
  for (I = 0; I < Count; I++) {
    Tables[I].Guid = gST->ConfigurationTable[I].VendorGuid;
  }
  for (I = 1; I < Count; I++) {
    for (J = I; J > 0 && CompareMem (&Tables[J - 1].Guid, &Tables[J].Guid,
                                     sizeof (EFI_GUID)) > 0; J--) {
      Swap = Tables[J - 1];
      Tables[J - 1] = Tables[J];
      Tables[J] = Swap;
    }
  }
  for (I = 0; I < Count; I++) {
    StFormatGuid (&Tables[I].Guid, GuidText,
                  sizeof (GuidText) / sizeof (GuidText[0]));
    AtReportAdd (Report, L"%s  %s", GuidText, TableName (&Tables[I].Guid));
  }
  FreePool (Tables);
  return EFI_SUCCESS;
}
