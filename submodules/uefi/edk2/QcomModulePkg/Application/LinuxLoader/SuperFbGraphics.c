/*
 * Soft Row GOP menu. Current mode only, UEFI-owned bounded surface, no direct
 * framebuffer access. Reacquire GOP at every screen boundary after a child.
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "SuperFbGraphics.h"
#include "SuperFbGraphicsFont.h"
#include <Protocol/GraphicsOutput.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define SFB_GFX_LIMIT (64U * 1024U * 1024U)
#define SFB_FONT_SCALE 2U
#define CELL_W (SFB_FONT_WIDTH * SFB_FONT_SCALE)
#define CELL_H (SFB_FONT_HEIGHT * SFB_FONT_SCALE)
#define LINE_STEP (CELL_H + 12U)
#define ROW_GAP 12U
#define FOOTER_GAP 27U
#define ROW_INSET 8U
#define ACCENT_WIDTH 5U

/* Website :root theme, retrieved 2026-11-18; BLT order is B,G,R,reserved.
 * background, card, shell-border, foreground, muted-foreground,
 * shell-accent and muted respectively. Flat fills, no interpolation.
 */
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mBackground = {0x17,0x06,0x02,0};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mHeader = {0x2A,0x17,0x0F,0};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mRule = {0x40,0x35,0x22,0};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mForeground = {0xF9,0xF5,0xF1,0};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mMuted = {0xB8,0xA3,0x94,0};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mAccent = {0xC5,0xD6,0x70,0};
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL mSelection = {0x3B,0x29,0x1E,0};
STATIC CONST CHAR16 mBrand[] = L"CANOE-BDS";
STATIC CONST CHAR16 mProduct[] = L"UEFI BOOT MANAGER";
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL *mPixels;
STATIC UINTN mBytes, mWidth, mHeight, mMargin, mY, mBottom;
STATIC UINTN mFooterTop, mRowHeight, mCountdownY, mCountdownHeight;
STATIC BOOLEAN mActive, mOverflow, mListStarted;
STATIC BOOLEAN mFailed;             /* Blt failure disables GOP for this session. */
STATIC BOOLEAN mRetry, mTextNext;    /* One complete text replay of a failed frame. */

BOOLEAN SfbGfxActive (VOID) { return mActive; }

VOID
SfbGfxRelease (VOID)
{
  mActive = FALSE;
  mGop = NULL;
  if (mPixels != NULL) { FreePool (mPixels); }
  mPixels = NULL;
  mBytes = 0;
  mRetry = FALSE;
  mTextNext = FALSE;
}

/* All geometry is bounded by SFB_GFX_LIMIT before this arithmetic is used. */
STATIC UINTN
Proportion (UINTN Value, UINTN PerThousand)
{
  return (Value * PerThousand + 500) / 1000;
}

STATIC VOID
Rect (UINTN X, UINTN Y, UINTN W, UINTN H,
      CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Colour)
{
  UINTN Row, Col;
  if (X >= mWidth || Y >= mHeight) { return; }
  if (W > mWidth - X) { W = mWidth - X; }
  if (H > mHeight - Y) { H = mHeight - Y; }
  for (Row = Y; Row < Y + H; Row++) {
    for (Col = X; Col < X + W; Col++) {
      mPixels[Row * mWidth + Col] = *Colour;
    }
  }
}

STATIC VOID
Line (CONST CHAR16 *Text, UINTN Length, UINTN X, UINTN Y,
      CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Colour)
{
  UINTN Char, Row, Bit;
  for (Char = 0; Char < Length; Char++) {
    UINTN Glyph = Text[Char] >= 32 && Text[Char] <= 126 ? Text[Char] - 32 : '?' - 32;
    for (Row = 0; Row < SFB_FONT_HEIGHT; Row++) {
      for (Bit = 0; Bit < SFB_FONT_WIDTH; Bit++) {
        if ((mSfbFont[Glyph][Row] & (0x8000U >> Bit)) != 0) {
          Rect (X + (Char * SFB_FONT_WIDTH + Bit) * SFB_FONT_SCALE,
                Y + Row * SFB_FONT_SCALE, SFB_FONT_SCALE, SFB_FONT_SCALE, Colour);
        }
      }
    }
  }
}

/* Newlines delimit lines; otherwise wrap at the last available word boundary. */
STATIC UINTN
LineLength (CONST CHAR16 *Text, UINTN Max, BOOLEAN Wrap)
{
  UINTN N = 0, Space = 0;
  while (N < Max && Text[N] != L'\0' && Text[N] != L'\r' && Text[N] != L'\n') {
    if (Text[N] == L' ') { Space = N; }
    N++;
  }
  return Wrap && N == Max && Text[N] != L'\0' &&
         Text[N] != L' ' && Text[N] != L'\r' && Text[N] != L'\n' &&
         Space != 0 ? Space : N;
}

STATIC CONST CHAR16 *
NextLine (CONST CHAR16 *Text, UINTN N)
{
  Text += N;
  while (*Text == L' ') { Text++; }
  if (*Text == L'\r') { Text++; }
  if (*Text == L'\n') { Text++; }
  return Text;
}

STATIC VOID
TextAt (CONST CHAR16 *Text, UINTN *Y, UINTN Limit, BOOLEAN Centered,
        BOOLEAN Wrap, CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Colour)
{
  UINTN Max = (mWidth - 2 * mMargin) / CELL_W;
  if (Text == NULL || *Text == L'\0') { return; }
  do {
    UINTN N, X;
    if (*Y >= Limit || CELL_H > Limit - *Y) { mOverflow = TRUE; break; }
    N = LineLength (Text, Max, Wrap);
    X = Centered ? (mWidth - N * CELL_W) / 2 : mMargin;
    if (!Wrap && Text[N] != L'\0' && N >= 3) {
      Line (Text, N - 3, X, *Y, Colour);
      Line (L"...", 3, X + (N - 3) * CELL_W, *Y, Colour);
    } else {
      Line (Text, N, X, *Y, Colour);
    }
    *Y += LINE_STEP;
    Text = NextLine (Text, N);
  } while (Wrap && *Text != L'\0');
}

STATIC VOID
TextFallback (BOOLEAN BltFailed)
{
  SfbGfxRelease ();
  if (BltFailed) { mFailed = TRUE; }
  else { mTextNext = TRUE; }
  mRetry = TRUE;
}

STATIC BOOLEAN
Present (UINTN Y, UINTN Height)
{
  EFI_STATUS Status;
  if (!mActive) { return FALSE; }
  Status = mGop->Blt (mGop, mPixels, EfiBltBufferToVideo,
                     0, Y, 0, Y, mWidth, Height, mWidth * sizeof (*mPixels));
  if (EFI_ERROR (Status)) {
    TextFallback (TRUE);
    gST->ConOut->ClearScreen (gST->ConOut);
    Print (L"Graphics unavailable (%r). Returning to text menus.\r\n", Status);
    return FALSE;
  }
  return TRUE;
}

STATIC VOID
DrawCountdown (UINT32 RemainingMs)
{
  CHAR16 Text[64];
  UINTN Y = mCountdownY;
  Rect (0, Y, mWidth, mCountdownHeight, &mBackground);
  if (RemainingMs == 0) {
    StrCpyS (Text, ARRAY_SIZE (Text), L"Select an entry to continue");
  } else {
    UnicodeSPrint (Text, sizeof (Text), L"Booting selection in %u s",
                   RemainingMs / 1000 + (RemainingMs % 1000 != 0));
  }
  TextAt (Text, &Y, mCountdownY + mCountdownHeight, FALSE, FALSE, &mAccent);
}

BOOLEAN
SfbGfxBegin (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle,
             IN CONST UINT32 *RemainingMs)
{
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  UINTN Width, Height, Bytes, Margin, HeaderBottom, BrandY, PageY, BrandWidth;
  mActive = FALSE;
  mCountdownHeight = 0;
  mOverflow = FALSE;
  mListStarted = FALSE;
  if (mFailed) { return FALSE; }
  if (mTextNext) { mTextNext = FALSE; return FALSE; }
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle,
                               &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR (Status) || Gop == NULL) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  }
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Blt == NULL ||
      Gop->Mode == NULL || Gop->Mode->Info == NULL ||
      Gop->Mode->SizeOfInfo < sizeof (*Gop->Mode->Info)) {
    SfbGfxRelease ();
    return FALSE;
  }
  Width = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;
  /* No inferred model/rotation or reduced font scale on exceptional modes.
   * Reserve enough portrait space for the header, info and a row.
   */
  if (Width < 480 || Width > SFB_GFX_LIMIT / sizeof (*mPixels) ||
      Height > SFB_GFX_LIMIT / sizeof (*mPixels) / Width || Height < Width ||
      Height - Width < 400) {
    SfbGfxRelease ();
    return FALSE;
  }
  Margin = Proportion (Width, 78);
  BrandY = Margin;
  /* Full-width text stays below the camera reserve.
   * This is a layout allowance, not device/cutout discovery through GOP.
   */
  if (BrandY < Proportion (Width, 135)) { BrandY = Proportion (Width, 135); }
  PageY = BrandY + CELL_H + 24;
  HeaderBottom = PageY + CELL_H + 40;
  Bytes = Width * Height * sizeof (*mPixels);
  if (Bytes != mBytes) {
    SfbGfxRelease ();
    mPixels = AllocatePool (Bytes);
    if (mPixels == NULL) { return FALSE; }
    mBytes = Bytes;
  }
  mGop = Gop;
  mWidth = Width;
  mHeight = Height;
  mMargin = Margin;
  mRowHeight = Proportion (Width, 85);
  if (mRowHeight < CELL_H + Proportion (CELL_H, 900)) {
    mRowHeight = CELL_H + Proportion (CELL_H, 900);
  }
  mFooterTop = Height - Proportion (Width, 245);
  /* On small modes preserve space for at least three help lines. */
  if (mFooterTop > Height - mMargin - 3 * LINE_STEP) {
    mFooterTop = Height - mMargin - 3 * LINE_STEP;
  }
  mBottom = mFooterTop - FOOTER_GAP;
  mY = HeaderBottom + Proportion (Width, 55);
  mActive = TRUE;
  if (gST->ConOut != NULL && gST->ConOut->EnableCursor != NULL) {
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }
  Rect (0, 0, Width, Height, &mBackground);
  Rect (0, 0, Width, HeaderBottom, &mHeader);
  Rect (0, HeaderBottom - 2, Width, 2, &mRule);
  BrandWidth = (ARRAY_SIZE (mBrand) - 1) * CELL_W;
  Line (mBrand, ARRAY_SIZE (mBrand) - 1, mMargin, BrandY, &mMuted);
  if (BrandWidth + (ARRAY_SIZE (mProduct) + 2) * CELL_W <= Width - 2 * Margin) {
    Line (L" / ", 3, mMargin + BrandWidth, BrandY, &mMuted);
    Line (mProduct, ARRAY_SIZE (mProduct) - 1,
          mMargin + BrandWidth + 3 * CELL_W, BrandY, &mMuted);
  }
  Rect (mMargin - 20, PageY + 4, ACCENT_WIDTH, CELL_H - 8, &mAccent);
  TextAt (Title, &PageY, HeaderBottom - 16, FALSE, FALSE, &mForeground);
  if (RemainingMs != NULL) {
    mCountdownY = mY;
    mCountdownHeight = CELL_H;
    DrawCountdown (*RemainingMs);
    mY += CELL_H + Proportion (Width, 65);
  }
  if (Subtitle != NULL) {
    TextAt (Subtitle, &mY, mBottom - mRowHeight, FALSE, TRUE, &mMuted);
  }
  mY += CELL_H / 2;
  return TRUE;
}

VOID
SfbGfxText (IN CONST CHAR16 *Text, IN BOOLEAN Centered, IN BOOLEAN Wrap)
{
  if (mActive) {
    UINTN Limit = mBottom;
    if (!mListStarted) { Limit = mBottom > mRowHeight ? mBottom - mRowHeight : 0; }
    TextAt (Text, &mY, Limit, Centered, Wrap, &mMuted);
  }
}

VOID
SfbGfxSelectionDetail (IN CONST CHAR16 *Text, IN BOOLEAN Wrap)
{
  UINTN Start, Padding;
  if (!mActive) { return; }
  Start = mY;
  SfbGfxText (Text, FALSE, Wrap);
  if (mY - Start < 2 * LINE_STEP) {
    Padding = 2 * LINE_STEP - (mY - Start);
    if (mY > mBottom || Padding > mBottom - mY) { mOverflow = TRUE; }
    else { mY += Padding; }
  }
}

VOID
SfbGfxGap (VOID)
{
  if (mActive && mY <= mBottom && ROW_GAP <= mBottom - mY) { mY += ROW_GAP; }
}

VOID
SfbGfxDivider (VOID)
{
  if (mActive && mY <= mBottom && ROW_GAP <= mBottom - mY) {
    /* The preceding row also leaves ROW_INSET pixels after its visible card.
     * Center between visible cards, not just within the inter-group gap.
     * Reuse the same gap height so pagination does not change.
     */
    Rect (mMargin, mY + (ROW_GAP - ROW_INSET - 2) / 2,
          mWidth - 2 * mMargin, 2, &mRule);
    mY += ROW_GAP;
  }
}

VOID
SfbGfxRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text)
{
  UINTN Max, N, X = mMargin + CELL_W, Y, Inset;
  CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Colour = Selected ? &mForeground : &mMuted;
  if (!mActive) { return; }
  mListStarted = TRUE;
  if (mY >= mBottom || mRowHeight > mBottom - mY) { mOverflow = TRUE; return; }
  Inset = mRowHeight - ROW_INSET;
  if (Selected) {
    Rect (mMargin, mY, mWidth - 2 * mMargin, Inset, &mSelection);
    Rect (mMargin, mY, ACCENT_WIDTH, Inset, &mAccent);
  }
  /* Keep the production default/notice marker, without shifting every label.
   * Reserve a trailing cell rather than dropping '*' / '!' as in the mockup.
   */
  Y = mY + (Inset - CELL_H) / 2;
  Max = (mWidth - 2 * mMargin - 3 * CELL_W) / CELL_W;
  N = LineLength (Text, Max, FALSE);
  if (Text[N] == L'\0' || N < 3) {
    Line (Text, N, X, Y, Colour);
  } else {
    Line (Text, N - 3, X, Y, Colour);
    Line (L"...", 3, X + (N - 3) * CELL_W, Y, Colour);
  }
  if (Marker != NULL && Marker[0] != L'\0' && Marker[0] != L' ') {
    Line (Marker, 1, mWidth - mMargin - 2 * CELL_W, Y, &mAccent);
  }
  mY += mRowHeight;
}

/* Footer measurement uses exactly the same word/newline rules as TextAt.
 * Bound the scan by the number of lines the panel could ever display.
 */
STATIC BOOLEAN
ReserveFooter (CONST CHAR16 *Footer)
{
  UINTN Lines = 0, Top, Needed;
  UINTN Max = (mWidth - 2 * mMargin) / CELL_W;
  if (Footer == NULL) { Footer = L""; }
  while (*Footer != L'\0') {
    if (++Lines > mHeight / LINE_STEP) { return FALSE; }
    Footer = NextLine (Footer, LineLength (Footer, Max, TRUE));
  }
  Needed = Lines * LINE_STEP + mMargin;
  if (Needed + FOOTER_GAP >= mHeight) { return FALSE; }
  Top = mHeight - Needed;
  if (Top < mFooterTop) { mFooterTop = Top; mBottom = Top - FOOTER_GAP; }
  return TRUE;
}

UINTN
SfbGfxRowsAvailable (IN CONST CHAR16 *Footer, IN UINTN ExtraRows)
{
  UINTN Reserve, Rows;
  if (!mActive) { return 1; }
  if (!ReserveFooter (Footer) || ExtraRows > mHeight / ROW_GAP) {
    mOverflow = TRUE;
    return 1;
  }
  /* Callers use ExtraRows for inter-group gaps. Also reserve the 'more' line.
   * This is conservative even when fewer of those gaps are actually drawn.
   */
  Reserve = ExtraRows * ROW_GAP + LINE_STEP;
  if (mY >= mBottom || Reserve >= mBottom - mY) {
    mOverflow = TRUE;
    return 1;
  }
  Rows = (mBottom - mY - Reserve) / mRowHeight;
  if (Rows == 0) { mOverflow = TRUE; return 1; }
  return Rows;
}

BOOLEAN
SfbGfxCountdown (IN UINT32 RemainingMs)
{
  if (!mActive || mCountdownHeight == 0) { return FALSE; }
  DrawCountdown (RemainingMs);
  return Present (mCountdownY, mCountdownHeight);
}

VOID
SfbGfxEnd (IN CONST CHAR16 *Footer)
{
  UINTN Y;
  if (!mActive) { return; }
  if (!ReserveFooter (Footer) || mY > mBottom || mOverflow) {
    /* Never publish a frame hiding a selected row or important information.
     * The common menu runner redraws this page with the legacy text renderer.
     */
    TextFallback (FALSE);
    return;
  }
  Y = mFooterTop;
  Rect (mMargin, mBottom, mWidth - 2 * mMargin, 1, &mRule);
  TextAt (Footer, &Y, mHeight - mMargin, FALSE, TRUE, &mMuted);
  if (mOverflow) { TextFallback (FALSE); return; }
  (VOID)Present (0, mHeight);
}

BOOLEAN
SfbGfxRetryText (VOID)
{
  BOOLEAN Retry = mRetry;
  mRetry = FALSE;
  return Retry;
}
