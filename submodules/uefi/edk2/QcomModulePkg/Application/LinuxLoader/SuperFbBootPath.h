/* Logical boot-volume paths shared by config and BLS readers. These are not
 * host file-picker paths. SPDX-License-Identifier: BSD-3-Clause */
#ifndef SUPER_FB_BOOT_PATH_H
#define SUPER_FB_BOOT_PATH_H
#include "Hook/SuperFbProfile.h"

static inline SFB_BOOLEAN
SfbBootNameValid (const char *Text, SFB_UINTN Length)
{
  SFB_UINTN I, Stem = 0;
  char Name[8];
  if (Length == 0 || Text[Length - 1] == '.' || Text[Length - 1] == ' ')
    return FALSE;
  for (I = 0; I < Length; I++) {
    char C = Text[I];
    if (C < 0x20 || C > 0x7e || C == '/' || C == '\\' || C == ':' ||
        C == '<' || C == '>' || C == '"' || C == '|' || C == '?' || C == '*')
      return FALSE;
  }
  while (Stem < Length && Text[Stem] != '.') {
    if (Stem < sizeof (Name)) {
      char C = Text[Stem];
      Name[Stem] = (C >= 'a' && C <= 'z') ? C - 'a' + 'A' : C;
    }
    Stem++;
  }
  if (Stem == 3 &&
      ((Name[0] == 'C' && Name[1] == 'O' && Name[2] == 'N') ||
       (Name[0] == 'P' && Name[1] == 'R' && Name[2] == 'N') ||
       (Name[0] == 'A' && Name[1] == 'U' && Name[2] == 'X') ||
       (Name[0] == 'N' && Name[1] == 'U' && Name[2] == 'L')))
    return FALSE;
  if (Stem == 4 && Name[3] >= '1' && Name[3] <= '9' &&
      ((Name[0] == 'C' && Name[1] == 'O' && Name[2] == 'M') ||
       (Name[0] == 'L' && Name[1] == 'P' && Name[2] == 'T')))
    return FALSE;
  if (Stem == 6 && Name[0] == 'C' && Name[1] == 'O' && Name[2] == 'N' &&
      Name[3] == 'I' && Name[4] == 'N' && Name[5] == '$')
    return FALSE;
  if (Stem == 7 && Name[0] == 'C' && Name[1] == 'O' && Name[2] == 'N' &&
      Name[3] == 'O' && Name[4] == 'U' && Name[5] == 'T' && Name[6] == '$')
    return FALSE;
  return TRUE;
}

static inline SFB_BOOLEAN
SfbBootPathValid (const char *Text, SFB_UINTN Length)
{
  SFB_UINTN Start = 0, I;
  if (Length > 0 && (Text[0] == '/' || Text[0] == '\\'))
    Start = 1;
  for (I = Start; I <= Length; I++) {
    if (I == Length || Text[I] == '/' || Text[I] == '\\') {
      if (!SfbBootNameValid (Text + Start, I - Start))
        return FALSE;
      Start = I + 1;
    }
  }
  return TRUE;
}
#endif
