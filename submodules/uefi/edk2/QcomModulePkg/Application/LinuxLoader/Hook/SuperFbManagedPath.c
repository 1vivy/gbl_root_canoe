#include "SuperFbManagedPath.h"

static SFB_PATH_CHAR16
SfbFoldPathChar (SFB_PATH_CHAR16 Character)
{
  if (Character >= 'A' && Character <= 'Z') {
    return (SFB_PATH_CHAR16)(Character + ('a' - 'A'));
  }
  return Character;
}

static SFB_PATH_BOOLEAN
SfbPathEquals (const SFB_PATH_CHAR16 *Left, const SFB_PATH_CHAR16 *Right)
{
  unsigned long Index;

  if (Left == NULL || Right == NULL) {
    return FALSE;
  }
  for (Index = 0; ; ++Index) {
    if (SfbFoldPathChar (Left[Index]) != SfbFoldPathChar (Right[Index])) {
      return FALSE;
    }
    if (Left[Index] == 0) {
      return TRUE;
    }
  }
}

SFB_PATH_BOOLEAN
SfbIsManagedAblPath (const SFB_PATH_CHAR16 *Path)
{
  static const SFB_PATH_CHAR16 Boot[] =
    {'\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i', 0};
  static const SFB_PATH_CHAR16 Backup[] =
    {'\\', 'b', 'o', 'o', 't', '_', 'b', 'a', 'c', 'k', 'u', 'p',
     '.', 'e', 'f', 'i', 0};
  static const SFB_PATH_CHAR16 PersistBoot[] =
    {'\\', 'e', 'f', 'i', 's', 'p', '\\', 'b', 'o', 'o', 't', '.',
     'e', 'f', 'i', 0};
  static const SFB_PATH_CHAR16 PersistBackup[] =
    {'\\', 'e', 'f', 'i', 's', 'p', '\\', 'b', 'o', 'o', 't', '_',
     'b', 'a', 'c', 'k', 'u', 'p', '.', 'e', 'f', 'i', 0};

  return SfbPathEquals (Path, Boot) ||
         SfbPathEquals (Path, Backup) ||
         SfbPathEquals (Path, PersistBoot) ||
         SfbPathEquals (Path, PersistBackup);
}
