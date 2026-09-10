/* Root observation uses ordinary EFI file status, never an integrity scan. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Guid/FileInfo.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfigStore.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBootRoot.h"

EFI_GUID gEfiFileInfoGuid;
static EFI_FILE_PROTOCOL Root, File;
static EFI_STATUS RootStatus, ConfigStatus;
static UINTN Allocations, LiveAllocations, FailAllocation;
static UINTN RootCloses, FileCloses;
static SFB_CONFIG Config;
typedef struct {
  CONST CHAR16 *Path;
  EFI_STATUS OpenStatus, SizeStatus, InfoStatus, CloseStatus;
  BOOLEAN Directory;
} TEST_FILE;
static TEST_FILE Files[4], *Opened;
static UINTN FileCount;

UINTN EFIAPI __StrLen (CONST CHAR16 *Text)
{
  UINTN Count = 0;
  while (Text[Count] != 0) { Count++; }
  return Count;
}
RETURN_STATUS EFIAPI
__StrnCpyS (CHAR16 *Out, UINTN Chars, CONST CHAR16 *In, UINTN Length)
{
  UINTN Index;
  for (Index = 0; Index < Length && In[Index] != 0; Index++) {
    assert(Index + 1 < Chars); Out[Index] = In[Index];
  }
  Out[Index] = 0;
  return RETURN_SUCCESS;
}
RETURN_STATUS EFIAPI
__StrCatS (CHAR16 *Out, UINTN Chars, CONST CHAR16 *In)
{
  UINTN Length = StrLen (Out);
  return __StrnCpyS (Out + Length, Chars - Length, In, StrLen (In));
}
VOID *EFIAPI AllocateZeroPool (UINTN Bytes)
{
  if (++Allocations == FailAllocation) { return NULL; }
  VOID *Buffer = calloc (1, Bytes);
  assert(Buffer != NULL); LiveAllocations++;
  return Buffer;
}
VOID EFIAPI FreePool (VOID *Buffer)
{
  assert(Buffer != NULL && LiveAllocations != 0);
  LiveAllocations--; free (Buffer);
}
static BOOLEAN SamePath (CONST CHAR16 *A, CONST CHAR16 *B)
{
  if (*A == L'\\') { A++; }
  if (*B == L'\\') { B++; }
  while (*A != 0 && *A == *B) { A++; B++; }
  return (BOOLEAN)(*A == *B);
}
static EFI_STATUS EFIAPI
Open (EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **Out, CHAR16 *Path,
      UINT64 Mode, UINT64 Attributes)
{
  assert(This == &Root && Mode == EFI_FILE_MODE_READ && Attributes == 0);
  *Out = NULL;
  for (UINTN Index = 0; Index < FileCount; Index++) {
    if (SamePath (Path, Files[Index].Path)) {
      Opened = &Files[Index];
      if (!EFI_ERROR (Opened->OpenStatus)) { *Out = &File; }
      return Opened->OpenStatus;
    }
  }
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI
GetInfo (EFI_FILE_PROTOCOL *This, EFI_GUID *Guid, UINTN *Bytes, VOID *Buffer)
{
  assert(This == &File && Guid == &gEfiFileInfoGuid && Opened != NULL);
  if (Buffer == NULL) {
    *Bytes = sizeof (EFI_FILE_INFO);
    return Opened->SizeStatus;
  }
  assert(*Bytes == sizeof (EFI_FILE_INFO));
  if (!EFI_ERROR (Opened->InfoStatus)) {
    ((EFI_FILE_INFO *)Buffer)->Attribute = Opened->Directory ? EFI_FILE_DIRECTORY : 0;
  }
  return Opened->InfoStatus;
}
static EFI_STATUS EFIAPI Close (EFI_FILE_PROTOCOL *This)
{
  if (This == &Root) { RootCloses++; return EFI_SUCCESS; }
  assert(This == &File && Opened != NULL); FileCloses++;
  return Opened->CloseStatus;
}
EFI_STATUS SfbContainerOpenRoot (EFI_FILE_PROTOCOL **Out)
{
  *Out = EFI_ERROR (RootStatus) ? NULL : &Root;
  return RootStatus;
}
EFI_STATUS
SfbReadStoredConfig (EFI_FILE_PROTOCOL *Volume, CHAR8 *Buffer, UINTN *Bytes,
                     SFB_CONFIG *Out, BOOLEAN *Previous)
{
  assert(Volume == &Root && Buffer != NULL);
  *Bytes = 0; *Previous = FALSE;
  if (!EFI_ERROR (ConfigStatus)) { memcpy (Out, &Config, sizeof (*Out)); }
  return ConfigStatus;
}
static TEST_FILE *AddFile (CONST CHAR16 *Path)
{
  assert(FileCount < ARRAY_SIZE (Files));
  TEST_FILE *Added = &Files[FileCount++];
  Added->Path = Path; Added->SizeStatus = EFI_BUFFER_TOO_SMALL;
  return Added;
}
static void Reset (void)
{
  assert(LiveAllocations == 0);
  memset (&Root, 0, sizeof Root); memset (&File, 0, sizeof File);
  memset (&Config, 0, sizeof Config); memset (Files, 0, sizeof Files);
  Root.Open = Open; Root.Close = Close; File.GetInfo = GetInfo; File.Close = Close;
  RootStatus = EFI_SUCCESS; ConfigStatus = EFI_NOT_FOUND;
  Allocations = FailAllocation = RootCloses = FileCloses = FileCount = 0;
  Opened = NULL;
}
static void Observe (SFB_BOOT_ROOT_STATE Expected)
{
  assert(SfbBootRootObserve () == Expected);
  assert(LiveAllocations == 0);
  assert(RootCloses == (EFI_ERROR (RootStatus) ? 0u : 1u));
}
int main (void)
{
  static CONST EFI_STATUS Errors[] = {
    EFI_DEVICE_ERROR, EFI_ACCESS_DENIED, EFI_OUT_OF_RESOURCES
  };
  BOOLEAN IsFile;

  Reset (); Observe (SfbBootRootEmptyRoot);
  Reset (); RootStatus = EFI_NOT_FOUND; Observe (SfbBootRootNoRoot);
  Reset (); RootStatus = EFI_DEVICE_ERROR; Observe (SfbBootRootUnavailable);
  Reset (); ConfigStatus = EFI_COMPROMISED_DATA; Observe (SfbBootRootEmptyRoot);
  Reset (); ConfigStatus = EFI_SUCCESS; Observe (SfbBootRootEmptyRoot); /* policy only */
  Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->Directory = TRUE;
  Observe (SfbBootRootEmptyRoot);

  /* Missing, malformed and unreadable are different observations. A readable
   * alternative still works even if another file cannot be inspected. */
  for (UINTN Index = 0; Index < ARRAY_SIZE (Errors); Index++) {
    Reset (); ConfigStatus = Errors[Index]; Observe (SfbBootRootUnavailable);
    Reset (); ConfigStatus = Errors[Index]; AddFile (SFB_MANAGED_SLOT_B_NAME);
    Observe (SfbBootRootPopulatedManaged);
    Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->OpenStatus = Errors[Index];
    Observe (SfbBootRootUnavailable);
    Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->OpenStatus = Errors[Index];
    AddFile (SFB_MANAGED_SLOT_B_NAME); Observe (SfbBootRootPopulatedManaged);
    Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->SizeStatus = Errors[Index];
    Observe (SfbBootRootUnavailable); assert(FileCloses == 1);
    Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->InfoStatus = Errors[Index];
    Observe (SfbBootRootUnavailable); assert(FileCloses == 1);
  }

  /* Both config allocations and file-info allocation preserve OOM. */
  for (UINTN Index = 1; Index <= 2; Index++) {
    Reset (); FailAllocation = Index; Observe (SfbBootRootUnavailable);
    Reset (); FailAllocation = Index; AddFile (SFB_MANAGED_SLOT_B_NAME);
    Observe (SfbBootRootPopulatedManaged);
  }
  Reset (); FailAllocation = 3; AddFile (SFB_MANAGED_SLOT_A_NAME);
  Observe (SfbBootRootUnavailable); assert(FileCloses == 1);

  Reset (); ConfigStatus = EFI_SUCCESS; Config.Count = 2;
  strcpy (Config.Entry[0].Image, "first.efi");
  strcpy (Config.Entry[1].Image, "second.efi");
  AddFile (L"first.efi")->InfoStatus = EFI_DEVICE_ERROR;
  Observe (SfbBootRootUnavailable);
  Reset (); ConfigStatus = EFI_SUCCESS; Config.Count = 2;
  strcpy (Config.Entry[0].Image, "first.efi");
  strcpy (Config.Entry[1].Image, "second.efi");
  AddFile (L"first.efi")->InfoStatus = EFI_DEVICE_ERROR; AddFile (L"second.efi");
  Observe (SfbBootRootPopulatedConfig);

  Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->CloseStatus = EFI_DEVICE_ERROR;
  Observe (SfbBootRootUnavailable);
  Reset (); AddFile (SFB_MANAGED_SLOT_A_NAME)->InfoStatus = EFI_ACCESS_DENIED;
  Files[0].CloseStatus = EFI_DEVICE_ERROR;
  assert(SfbFileProbe (&Root, SFB_MANAGED_SLOT_A_NAME, &IsFile) == EFI_ACCESS_DENIED);
  assert(!IsFile && FileCloses == 1 && LiveAllocations == 0);
  puts ("root observation: filesystem status and usable alternatives passed");
  return 0;
}
