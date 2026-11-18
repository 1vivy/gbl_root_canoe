/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_GRAPHICS_H
#define SUPER_FB_GRAPHICS_H
#include <Uefi.h>

/* Private menu renderer: no installed protocols, mode changes or input hooks. */
BOOLEAN SfbGfxBegin (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle OPTIONAL,
                     IN CONST UINT32 *RemainingMs OPTIONAL);
BOOLEAN SfbGfxActive (VOID);
VOID SfbGfxText (IN CONST CHAR16 *Text, IN BOOLEAN Centered, IN BOOLEAN Wrap);
/* Reserve two text rows for the selected entry's detail, even for short text.
 * Longer notices remain visible and may expand beyond that minimum.
 */
VOID SfbGfxSelectionDetail (IN CONST CHAR16 *Text, IN BOOLEAN Wrap);
VOID SfbGfxRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text);
VOID SfbGfxGap (VOID);
VOID SfbGfxDivider (VOID);
UINTN SfbGfxRowsAvailable (IN CONST CHAR16 *Footer, IN UINTN ExtraRows);
BOOLEAN SfbGfxCountdown (IN UINT32 RemainingMs);
VOID SfbGfxEnd (IN CONST CHAR16 *Footer);
/* A failed present requests one complete redraw through the text fallback. */
BOOLEAN SfbGfxRetryText (VOID);
/* Release the surface and cancel pending replay state, not the session's
 * permanent Blt-failure fallback. Safe before handing control to a child.
 */
VOID SfbGfxRelease (VOID);
#endif
