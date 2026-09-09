/* Production handoff-store I/O with failures at each durable boundary. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLastBoot.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbContainer.h"
static EFI_FILE_PROTOCOL Root, File;
static UINT8 Stored[SFB_LAST_BOOT_BYTES + 8], Record[SFB_LAST_BOOT_BYTES];
static UINTN StoredSize, Position, Mutation, FaultAt, ReadCount, FlushCount;
static BOOLEAN Present, MissingRoot, DenyMutations, FailRead, CorruptRead, ShortWrite;
VOID *EFIAPI CopyMem (VOID *A, CONST VOID *B, UINTN N) { return memcpy (A, B, N); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN N) { return memcmp (A, B, N); }
static BOOLEAN Fault (VOID) { return (BOOLEAN)(DenyMutations || ++Mutation == FaultAt); }
static EFI_STATUS EFIAPI Close (EFI_FILE_PROTOCOL *F) { assert (F == &File || F == &Root); return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Flush (EFI_FILE_PROTOCOL *F) { (void)F; FlushCount++; return Fault () ? EFI_DEVICE_ERROR : EFI_SUCCESS; }
static EFI_STATUS EFIAPI Delete (EFI_FILE_PROTOCOL *F) { assert (F == &File); if (Fault ()) return EFI_WARN_DELETE_FAILURE; Present = FALSE; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI Write (EFI_FILE_PROTOCOL *F, UINTN *Size, VOID *Bytes) {
  assert (F == &File && Present && Position + *Size <= sizeof (Stored));
  if (Fault ()) return EFI_DEVICE_ERROR;
  if (ShortWrite && *Size) --*Size;
  memcpy (Stored + Position, Bytes, *Size); Position += *Size;
  if (Position > StoredSize) StoredSize = Position;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Read (EFI_FILE_PROTOCOL *F, UINTN *Size, VOID *Bytes) {
  UINTN N;
  assert (F == &File && Present); ReadCount++;
  if (FailRead) return EFI_DEVICE_ERROR;
  N = StoredSize - Position; if (N > *Size) N = *Size;
  memcpy (Bytes, Stored + Position, N); Position += N; *Size = N;
  if (CorruptRead && N == SFB_LAST_BOOT_BYTES) ((UINT8 *)Bytes)[9] ^= 1;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open (EFI_FILE_PROTOCOL *R, EFI_FILE_PROTOCOL **Out, CHAR16 *Path, UINT64 Mode, UINT64 Attributes) {
  static CONST CHAR16 Wanted[] = L"\\last-boot";
  assert (R == &Root && Attributes == 0 && memcmp (Path, Wanted, sizeof (Wanted)) == 0);
  *Out = NULL;
  if (!Present && (Mode & EFI_FILE_MODE_CREATE)) {
    if (Fault ()) return EFI_DEVICE_ERROR;
    Present = TRUE; StoredSize = 0; memset (Stored, 0, sizeof (Stored));
  }
  if (!Present) return EFI_NOT_FOUND;
  Position = 0; *Out = &File; return EFI_SUCCESS;
}
EFI_STATUS SfbContainerOpenRoot (EFI_FILE_PROTOCOL **Out) { *Out = MissingRoot ? NULL : &Root; return MissingRoot ? EFI_NOT_FOUND : EFI_SUCCESS; }
EFI_STATUS SfbContainerFlush (VOID) { FlushCount++; return Fault () ? EFI_DEVICE_ERROR : EFI_SUCCESS; }
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLastBoot.c"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLastBootStore.c"
static void Reset (BOOLEAN Old) {
  UINT32 Sum; UINTN I;
  memset (&Root, 0, sizeof (Root)); memset (&File, 0, sizeof (File));
  Root.Open = Open; Root.Close = Close; Root.Flush = Flush;
  File.Close = Close; File.Delete = Delete; File.Write = Write; File.Read = Read; File.Flush = Flush;
  memset (Record, 0, sizeof (Record)); memcpy (Record, "CNLB", 4); Record[4] = 1; Record[5] = 1; Record[6] = Record[7] = 2; Record[9] = 1;
  Sum = SfbLastBootChecksum (Record); for (I = 0; I < 4; I++) Record[252 + I] = (UINT8)(Sum >> (8 * I));
  Present = Old; StoredSize = sizeof (Record); memcpy (Stored, Record, sizeof (Record));
  Position = Mutation = FaultAt = ReadCount = FlushCount = 0;
  MissingRoot = DenyMutations = FailRead = CorruptRead = ShortWrite = FALSE;
}
static BOOLEAN ValidStored (VOID) { return Present && SfbLastBootValid (Stored, StoredSize) && Stored[5] == SFB_LAST_BOOT_HANDOFF; }
int main (void) {
  UINTN Boundary, Total;
  Reset (TRUE); assert (SfbLastBootClear () == EFI_SUCCESS); assert (!Present && FlushCount >= 2);
  Reset (FALSE); assert (SfbLastBootWrite (Record) == EFI_SUCCESS); assert (ValidStored () && ReadCount == 1 && FlushCount >= 3); Total = Mutation;
  for (Boundary = 1; Boundary <= Total; Boundary++) {
    Reset (FALSE); FaultAt = Boundary;
    assert (SfbLastBootWrite (Record) == EFI_NOT_FOUND);
    assert (!ValidStored ());
  }
  Reset (FALSE); ShortWrite = TRUE; assert (SfbLastBootWrite (Record) == EFI_NOT_FOUND); assert (!ValidStored ());
  Reset (FALSE); CorruptRead = TRUE; assert (SfbLastBootWrite (Record) == EFI_NOT_FOUND); assert (!ValidStored ());
  Reset (TRUE); DenyMutations = TRUE; assert (EFI_ERROR (SfbLastBootClear ())); assert (ValidStored ());
  Reset (TRUE); FaultAt = 3; /* deletion fails after invalidation flushed */
  assert (SfbLastBootClear () == EFI_SUCCESS); assert (Present && !ValidStored ());
  Reset (TRUE); FailRead = TRUE; assert (EFI_ERROR (SfbLastBootClear ()) || !Present);
  Reset (TRUE); MissingRoot = TRUE; assert (SfbLastBootClear () == EFI_NOT_FOUND); assert (SfbLastBootWrite (Record) == EFI_NOT_FOUND); assert (Mutation == 0);
  Reset (TRUE); StoredSize = SFB_LAST_BOOT_BYTES + 1; DenyMutations = FALSE; assert (SfbLastBootClear () == EFI_SUCCESS); assert (!ValidStored ());
  puts ("PASS last-boot store: canonical path, absent root, invalidation, exact readback, checked failure cleanup");
  return 0;
}
