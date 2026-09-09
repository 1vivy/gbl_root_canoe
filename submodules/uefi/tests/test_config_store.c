/* Host filesystem protocol fixture: real production publication and reader,
 * with failure injection at every mutating protocol boundary. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfigStore.h"

EFI_GUID gEfiFileInfoGuid;
typedef struct {
  EFI_FILE_PROTOCOL Protocol;
  char Name[64];
  char Bytes[SFB_CONFIG_MAX_BYTES + 1];
  UINTN Size;
  BOOLEAN Present;
} FILE_FIXTURE;
static FILE_FIXTURE Files[16];
static EFI_FILE_PROTOCOL Root;
static UINTN FaultAt, Mutations, Allocations;
static BOOLEAN ShortWrite;
static EFI_STATUS CurrentReadError;
static const char Original[] = "version 1\ngeneration 4\nmode 1\ndefault a\nentry a\n image boot_a.efi\n mode 1\n";

UINTN EFIAPI __StrLen (CONST CHAR16 *Text) { UINTN N = 0; while (Text[N]) { N++; } return N; }
UINTN EFIAPI __StrSize (CONST CHAR16 *Text) { return (__StrLen (Text) + 1) * 2; }
RETURN_STATUS EFIAPI __StrCpyS (CHAR16 *Out, UINTN Capacity, CONST CHAR16 *Text) {
  UINTN N = __StrLen (Text); assert (N < Capacity); memcpy (Out, Text, (N + 1) * 2); return RETURN_SUCCESS;
}
VOID *EFIAPI AllocateZeroPool (UINTN Size) { Allocations++; return calloc (1, Size); }
VOID EFIAPI FreePool (VOID *Pointer) { assert (Allocations); Allocations--; free (Pointer); }
UINTN EFIAPI UnicodeSPrint (CHAR16 *Out, UINTN Bytes, CONST CHAR16 *Format, ...) {
  char Name[64]; UINTN I; va_list Args;
  (void)Format;
  va_start (Args, Format);
  snprintf (Name, sizeof (Name), "\\.canoe-cfg-stage-%08x", va_arg (Args, UINT32));
  va_end (Args);
  assert ((strlen (Name) + 1) * 2 <= Bytes);
  for (I = 0; I <= strlen (Name); I++) { Out[I] = (CHAR16)Name[I]; }
  return strlen (Name);
}
static void Narrow (char *Out, CONST CHAR16 *Path) {
  UINTN I = 0; if (*Path == L'\\') { Path++; }
  while (Path[I]) { assert (I < 63 && Path[I] < 128); Out[I] = (char)Path[I]; I++; }
  Out[I] = 0;
}
static FILE_FIXTURE *Find (const char *Name) {
  UINTN I; for (I = 0; I < 16; I++) { if (Files[I].Present && strcmp (Files[I].Name, Name) == 0) { return &Files[I]; } }
  return NULL;
}
static BOOLEAN Fault (void) { return (BOOLEAN)(++Mutations == FaultAt); }
static EFI_STATUS EFIAPI Close (EFI_FILE_PROTOCOL *File) { (void)File; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Flush (EFI_FILE_PROTOCOL *File) { (void)File; return Fault () ? EFI_DEVICE_ERROR : EFI_SUCCESS; }
static EFI_STATUS EFIAPI Delete (EFI_FILE_PROTOCOL *Protocol) {
  FILE_FIXTURE *File = (FILE_FIXTURE *)Protocol;
  if (Fault ()) { return EFI_WARN_DELETE_FAILURE; }
  File->Present = FALSE; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Write (EFI_FILE_PROTOCOL *Protocol, UINTN *Size, VOID *Bytes) {
  FILE_FIXTURE *File = (FILE_FIXTURE *)Protocol;
  if (Fault ()) { return EFI_DEVICE_ERROR; }
  assert (*Size <= sizeof (File->Bytes));
  if (ShortWrite && *Size) { (*Size)--; }
  memcpy (File->Bytes, Bytes, *Size); File->Size = *Size; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI SetInfo (EFI_FILE_PROTOCOL *Protocol, EFI_GUID *Guid, UINTN Size, VOID *Buffer) {
  FILE_FIXTURE *File = (FILE_FIXTURE *)Protocol; EFI_FILE_INFO *Info = Buffer; char Name[64];
  (void)Guid; (void)Size;
  if (Fault ()) { return EFI_DEVICE_ERROR; }
  Narrow (Name, Info->FileName);
  if (Find (Name)) { return EFI_ACCESS_DENIED; }
  strcpy (File->Name, Name); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open (EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **Out, CHAR16 *Path, UINT64 Mode, UINT64 Attributes) {
  char Name[64]; FILE_FIXTURE *File; UINTN I; (void)This; (void)Attributes;
  *Out = NULL; Narrow (Name, Path); File = Find (Name);
  if (File == NULL && (Mode & EFI_FILE_MODE_CREATE)) {
    if (Fault ()) { return EFI_DEVICE_ERROR; }
    for (I = 0; I < 16; I++) { if (!Files[I].Present) { File = &Files[I]; break; } }
    assert (File != NULL); memset (File, 0, sizeof (*File)); strcpy (File->Name, Name); File->Present = TRUE;
  }
  if (File == NULL) { return EFI_NOT_FOUND; }
  File->Protocol.Close = Close; File->Protocol.Write = Write; File->Protocol.Flush = Flush;
  File->Protocol.Delete = Delete; File->Protocol.SetInfo = SetInfo;
  *Out = &File->Protocol; return EFI_SUCCESS;
}
EFI_STATUS SfbReadFileBytes (EFI_FILE_PROTOCOL *This, CONST CHAR16 *Path, VOID *Buffer, UINTN Limit, UINTN *Read) {
  char Name[64]; FILE_FIXTURE *File; (void)This; Narrow (Name, Path);
  if (strcmp (Name, "canoe.cfg") == 0 && EFI_ERROR (CurrentReadError)) { return CurrentReadError; }
  File = Find (Name); if (!File) { return EFI_NOT_FOUND; }
  *Read = File->Size < Limit ? File->Size : Limit; memcpy (Buffer, File->Bytes, *Read); return EFI_SUCCESS;
}
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfig.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfigStore.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfigSave.c"

static void Put (const char *Name, const char *Bytes, UINTN Size) {
  UINTN I; FILE_FIXTURE *File = Find (Name);
  if (!File) { for (I = 0; I < 16; I++) { if (!Files[I].Present) { File = &Files[I]; break; } } }
  assert (File && Size <= sizeof (File->Bytes)); memset (File, 0, sizeof (*File));
  strcpy (File->Name, Name); memcpy (File->Bytes, Bytes, Size); File->Size = Size; File->Present = TRUE;
}
static void Reset (void) {
  assert (!Allocations); memset (Files, 0, sizeof (Files)); memset (&Root, 0, sizeof (Root));
  Root.Open = Open; Root.Flush = Flush; FaultAt = Mutations = 0; ShortWrite = FALSE; CurrentReadError = EFI_SUCCESS;
  Put ("canoe.cfg", Original, strlen (Original)); Put ("keep", "unrelated", 9);
}
static SFB_CONFIG ReadConfig (BOOLEAN *Previous) {
  SFB_CONFIG Config; char Bytes[SFB_CONFIG_MAX_BYTES + 1]; UINTN Size;
  assert (SfbReadStoredConfig (&Root, Bytes, &Size, &Config, Previous) == EFI_SUCCESS);
  return Config;
}
int main (void) {
  UINTN Boundary, Total; BOOLEAN Previous; SFB_CONFIG Config; FILE_FIXTURE *File;
  static const char BlsOnly[] = "version 1\ngeneration 9\nkey-window 2345\ndefault bls:linux\n";
  Reset ();
  Put ("canoe.cfg.prev", Original, strlen (Original));
  Put ("canoe.cfg", BlsOnly, strlen (BlsOnly));
  Config = ReadConfig (&Previous);
  assert (!Previous && Config.Count == 0 && Config.Generation == 9);
  assert (Config.DefaultIsBls && Config.KeyWindowMs == 2345);
  assert (SfbStoreConfigDefault (&Root, "bls:linux", 0) == EFI_SUCCESS);
  Config = ReadConfig (&Previous);
  assert (!Previous && Config.Count == 0 && Config.DefaultIsBls);
  Reset ();
  Put (".canoe-cfg-stage-00000001", "old interrupted staging", 23);
  assert (SfbStoreConfigDefault (&Root, "a", 2) == EFI_SUCCESS);
  Total = Mutations;
  assert (Find (".canoe-cfg-stage-00000001") != NULL);
  Config = ReadConfig (&Previous); assert (!Previous && Config.Generation == 5 && Config.Entry[0].Mode == 2);
  File = Find ("canoe.cfg.prev"); assert (File && File->Size == strlen (Original) && memcmp (File->Bytes, Original, File->Size) == 0);
  for (Boundary = 1; Boundary <= Total; Boundary++) {
    Reset (); FaultAt = Boundary;
    assert (EFI_ERROR (SfbStoreConfigDefault (&Root, "a", 2)));
    FaultAt = 0;
    Config = ReadConfig (&Previous);
    assert ((Config.Generation == 4 && Config.Entry[0].Mode == 1) || (Config.Generation == 5 && Config.Entry[0].Mode == 2));
    assert (memcmp (Find ("keep")->Bytes, "unrelated", 9) == 0 && !Allocations);
  }
  Reset (); ShortWrite = TRUE;
  assert (EFI_ERROR (SfbStoreConfigDefault (&Root, "a", 2)));
  Config = ReadConfig (&Previous); assert (!Previous && Config.Generation == 4);
  Reset (); Put ("canoe.cfg.prev", Original, strlen (Original)); Put ("canoe.cfg", "invalid", 7);
  Config = ReadConfig (&Previous); assert (Previous && Config.Generation == 4);
  assert (SfbStoreConfigDefault (&Root, "a", 2) == EFI_SUCCESS);
  assert (memcmp (Find ("canoe.cfg.prev")->Bytes, Original, strlen (Original)) == 0);
  Config = ReadConfig (&Previous); assert (!Previous && Config.Generation == 5);
  Find ("canoe.cfg")->Present = FALSE;
  Config = ReadConfig (&Previous); assert (Previous && Config.Generation == 4);
  CurrentReadError = EFI_ACCESS_DENIED;
  { char Bytes[SFB_CONFIG_MAX_BYTES + 1]; UINTN Size;
    assert (SfbReadStoredConfig (&Root, Bytes, &Size, &Config, &Previous) == EFI_ACCESS_DENIED); }
  assert (!Allocations);
  printf ("test_config_store: %lu mutation failure boundaries, fallback and preservation passed\n", (unsigned long)Total);
  return 0;
}
