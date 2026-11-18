/* GOP boundary: bounded allocations, supported geometry, child-return
 * reacquisition, checked presentation and text fallback. No aesthetic asserts.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include "../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbGraphics.h"

static EFI_BOOT_SERVICES Boot;
static EFI_SYSTEM_TABLE System;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Console;
static EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL OtherGop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *CurrentGop = &Gop;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
EFI_BOOT_SERVICES *gBS = &Boot;
EFI_SYSTEM_TABLE *gST = &System;
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static unsigned Presents, Acquires, Clears, Live, Allocations;
static UINTN LastY, LastHeight;
static int FailAlloc, FailBlt, Missing;
static UINT8 *Allocation;
static UINTN Allocated;
static const char *Preview;
static const char *CapturePrefix;
static char CapturePath[512];

VOID *EFIAPI AllocatePool (UINTN Size)
{
  if (FailAlloc) { return NULL; }
  assert(Live == 0 && Size <= 64U * 1024U * 1024U);
  Allocation = malloc(Size + 32);
  assert(Allocation);
  memset(Allocation, 0xa5, Size + 32);
  Allocated = Size;
  Allocations++;
  Live++;
  return Allocation + 16;
}

VOID EFIAPI FreePool (VOID *Buffer)
{
  assert(Live == 1 && Buffer == Allocation + 16);
  for (unsigned i = 0; i < 16; i++) {
    assert(Allocation[i] == 0xa5 && Allocation[Allocated + 16 + i] == 0xa5);
  }
  free(Allocation);
  Live--;
}

UINTN EFIAPI __StrLen (CONST CHAR16 *Text)
{
  UINTN N = 0;
  while (Text[N]) { N++; }
  return N;
}

RETURN_STATUS EFIAPI __StrCpyS (CHAR16 *Dest, UINTN Size, CONST CHAR16 *Src)
{
  assert(StrLen(Src) < Size);
  do { *Dest++ = *Src; } while (*Src++);
  return RETURN_SUCCESS;
}

UINTN EFIAPI UnicodeSPrint (CHAR16 *Buffer, UINTN Size, CONST CHAR16 *Format, ...)
{
  va_list Args;
  char Ascii[80], AsciiFormat[80];
  UINTN I = 0;
  for (; Format[I] != 0; I++) {
    assert(I + 1 < sizeof(AsciiFormat) && Format[I] < 128);
    AsciiFormat[I] = (char)Format[I];
  }
  AsciiFormat[I] = 0;
  va_start(Args, Format);
  int Result = vsnprintf(Ascii, sizeof(Ascii), AsciiFormat, Args);
  va_end(Args);
  assert(Result >= 0 && (unsigned)Result < sizeof(Ascii));
  UINTN N = strlen(Ascii);
  assert((N + 1) * sizeof(CHAR16) <= Size);
  for (UINTN I = 0; I <= N; I++) { Buffer[I] = Ascii[I]; }
  return N;
}

UINTN EFIAPI Print (CONST CHAR16 *Format, ...) { (void)Format; return 0; }

static EFI_STATUS EFIAPI Clear (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This)
{
  (void)This;
  Clears++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI Handle (EFI_HANDLE H, EFI_GUID *Guid, VOID **Out)
{
  (void)H;
  assert(Guid == &gEfiGraphicsOutputProtocolGuid);
  Acquires++;
  *Out = Missing ? NULL : CurrentGop;
  return Missing ? EFI_NOT_FOUND : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI Locate (EFI_GUID *Guid, VOID *Registration, VOID **Out)
{
  (void)Registration;
  return Handle(NULL, Guid, Out);
}

static EFI_STATUS EFIAPI Blt (EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixels, EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op,
  UINTN SX, UINTN SY, UINTN DX, UINTN DY, UINTN W, UINTN H, UINTN Delta)
{
  assert(This == CurrentGop && Op == EfiBltBufferToVideo);
  assert(SX == 0 && DX == 0 && SY == DY);
  assert(W == Info.HorizontalResolution && H <= Info.VerticalResolution);
  assert(SY <= Info.VerticalResolution - H);
  assert(Delta == W * sizeof(*Pixels) && Allocated == Delta * Info.VerticalResolution);
  Presents++;
  LastY = DY; LastHeight = H;
  if (Preview) {
    FILE *F = fopen(Preview, "wb");
    assert(F);
    fprintf(F, "P6\n%lu %u\n255\n", (unsigned long)W, Info.VerticalResolution);
    for (UINTN I = 0; I < W * Info.VerticalResolution; I++) {
      fputc(Pixels[I].Red, F); fputc(Pixels[I].Green, F); fputc(Pixels[I].Blue, F);
    }
    fclose(F);
  }
  return FailBlt ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static void Screen(unsigned W, unsigned H, int Paged)
{
  static const CHAR16 *Rows[] = {
    L"Android A (current slot)", L"Android B", L"USB Mass Storage",
    L"Advanced >", L"Reboot >", L"Power off", L"Restart"
  };
  UINT32 Remaining = 3000;
  Info.HorizontalResolution = W; Info.VerticalResolution = H;
  assert(SfbGfxBegin(Paged ? L"A longer dynamic menu title that wraps without losing the selected row" : L"Select boot target",
                     L"gbl_root_canoe by 1vivy", &Remaining));
  SfbGfxText(L"Mode 1 - Android locked", FALSE, FALSE);
  SfbGfxSelectionDetail(L"boot_a.efi", FALSE);
  SfbGfxGap();
  UINTN Count = SfbGfxRowsAvailable(L"Vol Up/Down: move   Power: select", 5);
  assert(Count >= 1);
  if (!Paged && Count > sizeof(Rows)/sizeof(Rows[0])) { Count = sizeof(Rows)/sizeof(Rows[0]); }
  for (UINTN I = 0; I < Count; I++) {
    if (I >= 2 && I < 7) { SfbGfxDivider(); }
    SfbGfxRow(Paged ? I == Count - 1 : I == 0, I == 0 ? L"*" : L" ",
              Paged && I == Count - 1 ? L"A very long selected loader entry that must be ellipsized safely instead of overlapping the footer" : Rows[I % 7]);
  }
  if (Paged) { SfbGfxText(L"... more entries", FALSE, FALSE); }
  if (CapturePrefix) {
    snprintf(CapturePath, sizeof(CapturePath), "%s-%ux%u-%s.ppm", CapturePrefix, W, H, Paged ? "paged" : "main");
    Preview = CapturePath;
  }
  SfbGfxEnd(L"Vol Up/Down: move   Power: select");
  Preview = NULL;
  assert(SfbGfxActive());
  SfbGfxRelease();
  assert(Live == 0);
}

int main(int Argc, char **Argv)
{
  Boot.HandleProtocol = Handle; Boot.LocateProtocol = Locate;
  System.ConOut = &Console; Console.ClearScreen = Clear;
  Mode.Info = &Info; Mode.SizeOfInfo = sizeof(Info);
  Gop.Mode = &Mode; Gop.Blt = Blt; OtherGop = Gop;
  Info.HorizontalResolution = 1264; Info.VerticalResolution = 2800;
  Missing = 1;
  assert(!SfbGfxBegin(L"Unavailable", NULL, NULL) && Live == 0);
  Missing = 0; FailAlloc = 1;
  assert(!SfbGfxBegin(L"No memory", NULL, NULL) && Live == 0);
  FailAlloc = 0;
  /* Unsupported orientation/compact modes must use legacy text, not rotate
   * the panel or silently rescale the required 24x48 glyph cells. */
  Info.HorizontalResolution = 320; Info.VerticalResolution = 480;
  assert(!SfbGfxBegin(L"Compact", NULL, NULL) && Live == 0);
  Info.HorizontalResolution = 1920; Info.VerticalResolution = 1080;
  assert(!SfbGfxBegin(L"Landscape", NULL, NULL) && Live == 0);
  Screen(720, 1280, 0);
  CurrentGop = &OtherGop; /* A child may replace the installed protocol. */
  CapturePrefix = Argc > 1 ? Argv[1] : NULL;
  Screen(1272, 2772, 0); Screen(1272, 2772, 1);
  Screen(1272, 2800, 0); Screen(1272, 2800, 1);
  Screen(1216, 2640, 0); Screen(1216, 2640, 1);
  /* A short selection detail and a two-line detail reserve the same list
   * capacity. Changing selection must not change ordinary menu pagination.
   */
  Info.HorizontalResolution = 720; Info.VerticalResolution = 1280;
  assert(SfbGfxBegin(L"Selection detail", NULL, NULL));
  SfbGfxText(L"Entry", FALSE, FALSE);
  SfbGfxSelectionDetail(L"Short", TRUE);
  UINTN DetailRows = SfbGfxRowsAvailable(L"Power: select", 0);
  SfbGfxRelease();
  assert(SfbGfxBegin(L"Selection detail", NULL, NULL));
  SfbGfxText(L"Entry", FALSE, FALSE);
  SfbGfxSelectionDetail(L"First line\nSecond line", TRUE);
  assert(SfbGfxRowsAvailable(L"Power: select", 0) == DetailRows);
  SfbGfxRelease();
  Info.HorizontalResolution = UINT32_MAX;
  Info.VerticalResolution = UINT32_MAX;
  assert(!SfbGfxBegin(L"Bad geometry", NULL, NULL) && Live == 0);
  /* Huge footer cannot hide rows. Retry this page through text exactly once,
   * without disabling graphics for the next (normally sized) page. */
  Info.HorizontalResolution = 720; Info.VerticalResolution = 1280;
  assert(SfbGfxBegin(L"Footer overflow", NULL, NULL));
  SfbGfxRow(TRUE, L"!", L"Selected");
  SfbGfxEnd(L"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22");
  assert(!SfbGfxActive() && Live == 0 && SfbGfxRetryText());
  assert(!SfbGfxRetryText() && !SfbGfxBegin(L"Text retry", NULL, NULL));
  /* A long but fitting help block expands its reserved area before the list.
   * The renderer must still publish a selectable row rather than overlap it.
   */
  assert(SfbGfxBegin(L"Long help", NULL, NULL));
  assert(SfbGfxRowsAvailable(L"First line\nSecond line\nThird line\nFourth line\nFifth line\nSixth line", 0) >= 1);
  SfbGfxRow(TRUE, L"!", L"Still visible");
  SfbGfxEnd(L"First line\nSecond line\nThird line\nFourth line\nFifth line\nSixth line");
  assert(SfbGfxActive() && !SfbGfxRetryText());
  SfbGfxRelease();
  Info.HorizontalResolution = 720; Info.VerticalResolution = 1280;
  assert(SfbGfxBegin(L"Countdown", NULL, &(UINT32){2000}));
  unsigned Before = Allocations;
  assert(SfbGfxCountdown(1000));
  assert(Allocations == Before && LastY != 0 && LastHeight < Info.VerticalResolution);
  FailBlt = 1;
  assert(!SfbGfxCountdown(0));
  assert(!SfbGfxActive() && Live == 0 && Clears == 1);
  assert(SfbGfxRetryText() && !SfbGfxRetryText());
  assert(!SfbGfxBegin(L"Text fallback", NULL, NULL));
  assert(Presents > 0 && Acquires >= 8);
  puts("graphics: bounded buffers, geometry, protocol reacquisition and fallback passed");
  return 0;
}
