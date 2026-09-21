/* Log rotation ordering against a fake logfs directory with no usable clock. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/SimpleFileSystem.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLog.h"

#define SLOTS  3
#define BYTES  512

typedef struct {
  EFI_FILE_PROTOCOL Protocol;
  UINTN             Index;
} FAKE_FILE;

static char     Content[SLOTS][BYTES];
static BOOLEAN  Present[SLOTS];
static UINTN    Deleted[SLOTS];
static UINTN    Created[SLOTS];

VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset(Buffer, 0, Size); }
VOID *EFIAPI CopyMem (VOID *Out, CONST VOID *In, UINTN Size) { return memcpy(Out, In, Size); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN Size) { return memcmp(A, B, Size); }

UINTN EFIAPI UnicodeSPrint (CHAR16 *Out, UINTN Size, CONST CHAR16 *Format, ...) {
  /* The production caller formats only L"bds-%u.log"; the index is recovered
     from the name by the fake Open, so the digit is all that has to survive. */
  va_list Args;
  unsigned Value;
  UINTN I = 0;
  (void)Size;
  va_start(Args, Format);
  Value = va_arg(Args, unsigned);
  va_end(Args);
  Out[I++] = 'b'; Out[I++] = 'd'; Out[I++] = 's'; Out[I++] = '-';
  Out[I++] = (CHAR16)('0' + Value);
  Out[I++] = '.'; Out[I++] = 'l'; Out[I++] = 'o'; Out[I++] = 'g'; Out[I] = 0;
  return I;
}

static EFI_STATUS EFIAPI FileRead (EFI_FILE_PROTOCOL *This, UINTN *Size, VOID *Buffer) {
  FAKE_FILE *File = (FAKE_FILE *)This;
  UINTN Length = strlen(Content[File->Index]);
  if (Length < *Size) { *Size = Length; }
  memcpy(Buffer, Content[File->Index], *Size);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FileClose (EFI_FILE_PROTOCOL *This) { free(This); return EFI_SUCCESS; }
static EFI_STATUS EFIAPI FileDelete (EFI_FILE_PROTOCOL *This) {
  FAKE_FILE *File = (FAKE_FILE *)This;
  Present[File->Index] = FALSE;
  Content[File->Index][0] = '\0';
  Deleted[File->Index]++;
  free(This);
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI DirOpen (EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **New,
                                  CHAR16 *Name, UINT64 Mode, UINT64 Attributes) {
  FAKE_FILE *File;
  UINTN Index = (UINTN)(Name[4] - '0');
  (void)This; (void)Attributes;
  assert(Index < SLOTS);
  if (!Present[Index] && (Mode & EFI_FILE_MODE_CREATE) == 0) {
    return EFI_NOT_FOUND;
  }
  if ((Mode & EFI_FILE_MODE_CREATE) != 0 && !Present[Index]) {
    Present[Index] = TRUE;
    Created[Index]++;
  }
  File = calloc(1, sizeof(*File));
  File->Index = Index;
  File->Protocol.Read = FileRead;
  File->Protocol.Close = FileClose;
  File->Protocol.Delete = FileDelete;
  *New = &File->Protocol;
  return EFI_SUCCESS;
}

static void Seed (UINTN Index, const char *Header) {
  Present[Index] = (BOOLEAN)(Header != NULL);
  Deleted[Index] = Created[Index] = 0;
  Content[Index][0] = '\0';
  if (Header != NULL) { strcpy(Content[Index], Header); }
}

static UINTN OpenSlot (EFI_FILE_PROTOCOL *Dir, UINT64 *Sequence) {
  EFI_FILE_PROTOCOL *File = ((VOID *)0);
  UINTN Index;
  assert(SfbLogOpenSlot(Dir, &File, Sequence) == EFI_SUCCESS);
  assert(File != ((VOID *)0));
  Index = ((FAKE_FILE *)File)->Index;
  File->Close(File);
  return Index;
}

int main (void) {
  EFI_FILE_PROTOCOL Dir;
  EFI_FILE_PROTOCOL *File;
  UINT64 Sequence;
  UINTN Index;

  memset(&Dir, 0, sizeof Dir);
  Dir.Open = DirOpen;

  /* A free slot is used before any occupied one, and outranks what it found. */
  Seed(0, "Canoe BDS session; seq=7; tag=pre-launch; captured-bytes=10\r\n");
  Seed(1, "Canoe BDS session; seq=9; tag=pre-launch; captured-bytes=10\r\n");
  Seed(2, NULL);
  assert(OpenSlot(&Dir, &Sequence) == 2);
  assert(Sequence == 10);

  /* All taken: the lowest sequence is evicted, never the highest. */
  Seed(0, "Canoe BDS session; seq=41; tag=a; captured-bytes=1\r\n");
  Seed(1, "Canoe BDS session; seq=12; tag=b; captured-bytes=1\r\n");
  Seed(2, "Canoe BDS session; seq=40; tag=c; captured-bytes=1\r\n");
  assert(OpenSlot(&Dir, &Sequence) == 1);
  assert(Sequence == 42 && Deleted[1] == 1 && Deleted[0] == 0 && Deleted[2] == 0);

  /* The defect this replaces: without a clock the newest file can look oldest,
     so repeated sessions must still walk every slot rather than pin to one. */
  Seed(0, "Canoe BDS session; seq=1; tag=a; captured-bytes=1\r\n");
  Seed(1, "Canoe BDS session; seq=2; tag=b; captured-bytes=1\r\n");
  Seed(2, "Canoe BDS session; seq=3; tag=c; captured-bytes=1\r\n");
  for (Index = 0; Index < SLOTS; Index++) {
    UINTN Chosen = OpenSlot(&Dir, &Sequence);
    assert(Chosen == Index);
    assert(Sequence == (UINT64)(4 + Index));
    sprintf(Content[Chosen], "Canoe BDS session; seq=%u; tag=x; captured-bytes=1\r\n",
            (unsigned)Sequence);
  }

  /* A header-less or truncated slot sorts first: a corrupt log is the one to reuse. */
  Seed(0, "Canoe BDS session; seq=5; tag=a; captured-bytes=1\r\n");
  Seed(1, "garbage with no sequence at all\r\n");
  Seed(2, "Canoe BDS session; seq=6; tag=c; captured-bytes=1\r\n");
  assert(OpenSlot(&Dir, &Sequence) == 1);
  assert(Sequence == 7);

  /* seq= present but non-numeric is not silently read as a partial number. */
  Seed(0, "Canoe BDS session; seq=x; tag=a; captured-bytes=1\r\n");
  Seed(1, "Canoe BDS session; seq=3; tag=b; captured-bytes=1\r\n");
  Seed(2, "Canoe BDS session; seq=4; tag=c; captured-bytes=1\r\n");
  assert(OpenSlot(&Dir, &Sequence) == 0);
  assert(Sequence == 5);

  /* Every slot missing is a usable state, not an error. */
  Seed(0, NULL); Seed(1, NULL); Seed(2, NULL);
  assert(OpenSlot(&Dir, &Sequence) == 0);
  assert(Sequence == 1 && Created[0] == 1);

  File = &Dir;
  assert(SfbLogOpenSlot(((VOID *)0), &File, &Sequence) == EFI_INVALID_PARAMETER);
  assert(SfbLogOpenSlot(&Dir, ((VOID *)0), &Sequence) == EFI_INVALID_PARAMETER);
  assert(SfbLogOpenSlot(&Dir, &File, ((VOID *)0)) == EFI_INVALID_PARAMETER);

  puts("log slot: sequence rotation, corrupt-slot eviction and free-slot reuse passed");
  return 0;
}
