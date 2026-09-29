#include "SuperFbBootRoot.h"

SFB_BOOLEAN
SfbBootRootFormat (
  SFB_BOOT_ROOT_STATE  State,
  char                *Buffer,
  SFB_UINTN            BufferBytes
  )
{
  static const char *const Reasons[] = {
    "populated-config",
    "populated-managed",
    "empty-root",
    "no-root",
    "no-volumes"
  };
  const char *Reason;
  SFB_UINTN Length;
  SFB_UINTN Index;

  if ((SFB_UINT32)State > (SFB_UINT32)SfbBootRootNoVolumes || Buffer == NULL) {
    return FALSE;
  }
  Reason = Reasons[State];
  for (Length = 0; Reason[Length] != '\0'; ++Length) {
  }
  if (BufferBytes <= Length) {
    return FALSE;
  }
  for (Index = 0; Index <= Length; ++Index) {
    Buffer[Index] = Reason[Index];
  }
  return TRUE;
}

SFB_BOOLEAN
SfbBootRootIsEmptyState (SFB_BOOT_ROOT_STATE State)
{
  return (SFB_BOOLEAN)(State == SfbBootRootEmptyRoot ||
                       State == SfbBootRootNoRoot ||
                       State == SfbBootRootNoVolumes);
}

#ifndef SFB_HOST_BUILD
#include <Library/UefiBootServicesTableLib.h>

#define SFB_BOOT_ROOT_TABLE_GUID \
  { 0x7c9ad6e1, 0x4f3b, 0x4a2d, \
    { 0x9e, 0x51, 0x63, 0xb8, 0x2a, 0x77, 0x0c, 0x14 } }
#define SFB_BOOT_ROOT_TABLE_MAGIC    SIGNATURE_32 ('C', 'N', 'B', 'R')
#define SFB_BOOT_ROOT_TABLE_VERSION  1u

typedef struct {
  UINT32  Magic;
  UINT32  Version;
  UINT32  State;
  UINT32  Reserved;
} SFB_BOOT_ROOT_TABLE;

STATIC SFB_BOOT_ROOT_OBSERVATION  gBootRootObservation = {
  FALSE,
  SfbBootRootNoVolumes
};
STATIC CONST EFI_GUID       mSfbBootRootTableGuid = SFB_BOOT_ROOT_TABLE_GUID;
STATIC SFB_BOOT_ROOT_TABLE  mSfbBootRootTable;

VOID
SfbRecordBootRootState (IN SFB_BOOT_ROOT_STATE State)
{
  gBootRootObservation.Available = TRUE;
  gBootRootObservation.State = State;
}

SFB_BOOT_ROOT_OBSERVATION
SfbGetBootRootState (VOID)
{
  return gBootRootObservation;
}

VOID
SfbPublishBootRootTable (VOID)
{
  if (!gBootRootObservation.Available) {
    return;
  }

  mSfbBootRootTable.Magic = SFB_BOOT_ROOT_TABLE_MAGIC;
  mSfbBootRootTable.Version = SFB_BOOT_ROOT_TABLE_VERSION;
  mSfbBootRootTable.State = (UINT32)gBootRootObservation.State;
  mSfbBootRootTable.Reserved = 0;
  gBS->InstallConfigurationTable ((EFI_GUID *)&mSfbBootRootTableGuid,
                                  &mSfbBootRootTable);
}
#endif
