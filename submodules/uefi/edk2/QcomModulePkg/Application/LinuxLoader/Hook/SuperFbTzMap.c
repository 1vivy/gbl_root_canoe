#include "SuperFbTzMap.h"
#include "SuperFbProfileRewrite.h"

static SFB_UINT16
SfbTzMapRead16 (const SFB_UINT8 *Bytes)
{
  return (SFB_UINT16)Bytes[0] | ((SFB_UINT16)Bytes[1] << 8);
}

static SFB_UINT32
SfbTzMapRead32 (const SFB_UINT8 *Bytes)
{
  return (SFB_UINT32)Bytes[0] |
         ((SFB_UINT32)Bytes[1] << 8) |
         ((SFB_UINT32)Bytes[2] << 16) |
         ((SFB_UINT32)Bytes[3] << 24);
}


static void
SfbTzMapCopy (SFB_UINT8 *Destination, const SFB_UINT8 *Source, SFB_UINTN Length)
{
  SFB_UINTN Index;

  for (Index = 0; Index < Length; ++Index) {
    Destination[Index] = Source[Index];
  }
}

static void
SfbTzMapZero (SFB_UINT8 *Bytes, SFB_UINTN Length)
{
  SFB_UINTN Index;

  for (Index = 0; Index < Length; ++Index) {
    Bytes[Index] = 0;
  }
}

static void
SfbTzMapSetCommand (
  SFB_TZ_MAP *Map,
  SFB_UINTN   Index,
  SFB_UINT16  Command,
  SFB_UINT16  RequestBytes,
  SFB_UINT8   Semantic
  )
{
  Map->Commands[Index].Command = Command;
  Map->Commands[Index].RequestBytes = RequestBytes;
  Map->Commands[Index].Semantic = Semantic;
  Map->Commands[Index].Occurrences = 0;
  Map->Commands[Index].Reserved = 0;
}

SFB_BOOLEAN
SfbTzMapParse (
  const SFB_UINT8 *Bytes,
  SFB_UINTN        Size,
  SFB_TZ_MAP      *Map
  )
{
  SFB_UINT16 CommandCount;
  SFB_UINTN Index;
  SFB_UINTN Byte;
  SFB_UINTN Offset;
  SFB_UINT16 Command;

  if (Map == NULL) {
    return FALSE;
  }

  SfbTzMapZero ((SFB_UINT8 *)Map, sizeof (*Map));
  if (Bytes == NULL || Size != SFB_TZMAP_BYTES) {
    return FALSE;
  }

  if (Bytes[0] != 'G' || Bytes[1] != 'T' ||
      Bytes[2] != 'Z' || Bytes[3] != 'M' ||
      SfbTzMapRead16 (Bytes + 4) != SFB_TZMAP_VERSION ||
      SfbTzMapRead32 (Bytes + 12) != 0 ||
      (SfbTzMapRead32 (Bytes + 8) & ~SFB_TZMAP_FLAG_ALL) != 0) {
    return FALSE;
  }

  for (Index = 0; Index < 80; ++Index) {
    if (Bytes[176 + Index] != 0) {
      return FALSE;
    }
  }

  CommandCount = SfbTzMapRead16 (Bytes + 6);
  if (CommandCount > SFB_TZMAP_MAX_COMMANDS) {
    return FALSE;
  }

  for (Index = 0; Index < CommandCount; ++Index) {
    Offset = 48 + (Index * sizeof (SFB_TZ_COMMAND));
    Command = SfbTzMapRead16 (Bytes + Offset);
    if (Command == 0 || Bytes[Offset + 4] > SFB_TZ_SEMANTIC_MAX ||
        SfbTzMapRead16 (Bytes + Offset + 6) != 0 ||
        (Index > 0 && Command <=
         SfbTzMapRead16 (Bytes + (Offset - sizeof (SFB_TZ_COMMAND))))) {
      return FALSE;
    }
  }

  for (Index = CommandCount; Index < SFB_TZMAP_MAX_COMMANDS; ++Index) {
    Offset = 48 + (Index * sizeof (SFB_TZ_COMMAND));
    for (Byte = 0; Byte < sizeof (SFB_TZ_COMMAND); ++Byte) {
      if (Bytes[Offset + Byte] != 0) {
        return FALSE;
      }
    }
  }

  SfbTzMapCopy (Map->Magic, Bytes, sizeof (Map->Magic));
  Map->Version = SfbTzMapRead16 (Bytes + 4);
  Map->CommandCount = CommandCount;
  Map->Flags = SfbTzMapRead32 (Bytes + 8);
  Map->Reserved0 = SfbTzMapRead32 (Bytes + 12);
  SfbTzMapCopy (Map->AblDigest, Bytes + 16, sizeof (Map->AblDigest));
  for (Index = 0; Index < CommandCount; ++Index) {
    Offset = 48 + (Index * sizeof (SFB_TZ_COMMAND));
    Map->Commands[Index].Command = SfbTzMapRead16 (Bytes + Offset);
    Map->Commands[Index].RequestBytes = SfbTzMapRead16 (Bytes + Offset + 2);
    Map->Commands[Index].Semantic = Bytes[Offset + 4];
    Map->Commands[Index].Occurrences = Bytes[Offset + 5];
    Map->Commands[Index].Reserved = SfbTzMapRead16 (Bytes + Offset + 6);
  }
  SfbTzMapCopy (Map->Reserved1, Bytes + 176, sizeof (Map->Reserved1));
  return TRUE;
}

const SFB_TZ_COMMAND *
SfbTzMapFind (
  const SFB_TZ_MAP *Map,
  SFB_UINT32        Command
  )
{
  SFB_UINTN Index;

  if (Map == NULL || Map->CommandCount > SFB_TZMAP_MAX_COMMANDS ||
      Command > 0xFFFFu) {
    return NULL;
  }

  for (Index = 0; Index < Map->CommandCount; ++Index) {
    if (Map->Commands[Index].Command == (SFB_UINT16)Command) {
      return &Map->Commands[Index];
    }
  }
  return NULL;
}

/* Semantics are firmware-owned; the sidecar is EVIDENCE only.
 *
 * The sidecar lives on a device-writable partition, so if it could assign
 * semantics a crafted file could reclassify WRITE_DEVICE_STATE to something
 * harmless and switch off a suppression the firmware applies unconditionally,
 * or attach a rewrite semantic to an unrelated command. Resolving the semantic
 * from the built-in protocol table whenever the command is one the firmware
 * knows removes that lever entirely: a sidecar can still contribute observed
 * request sizes, occurrence counts and additional command ids, but every id the
 * firmware already understands keeps the firmware's own classification.
 */
SFB_UINT8
SfbTzMapSemantic (
  const SFB_TZ_MAP *Map,
  SFB_UINT32        Command
  )
{
  SFB_TZ_MAP Builtin;
  const SFB_TZ_COMMAND *Known;

  SfbTzMapBuiltinDefault (&Builtin);
  Known = SfbTzMapFind (&Builtin, Command);
  if (Known != NULL) {
    return Known->Semantic;
  }

  /* Not a protocol command: the sidecar may only ever report it as UNKNOWN,
   * which keeps it on the pass-through path. */
  Known = SfbTzMapFind (Map, Command);
  if (Known != NULL) {
    return SFB_TZ_SEMANTIC_UNKNOWN;
  }
  return SFB_TZ_SEMANTIC_UNKNOWN;
}

void
SfbTzMapBuiltinDefault (
  SFB_TZ_MAP *Map
  )
{
  if (Map == NULL) {
    return;
  }

  SfbTzMapZero ((SFB_UINT8 *)Map, sizeof (*Map));
  Map->Magic[0] = 'G';
  Map->Magic[1] = 'T';
  Map->Magic[2] = 'Z';
  Map->Magic[3] = 'M';
  Map->Version = SFB_TZMAP_VERSION;
  Map->Flags = SFB_TZMAP_FLAG_ALL;
  Map->CommandCount = 9;
  SfbTzMapSetCommand (Map, 0, 0x200u, 0, SFB_TZ_SEMANTIC_GET_VERSION);
  SfbTzMapSetCommand (Map, 1, 0x201u, SFB_KM_SET_ROT_BYTES, SFB_TZ_SEMANTIC_SET_ROT);
  SfbTzMapSetCommand (Map, 2, 0x202u, 0, SFB_TZ_SEMANTIC_READ_DEVICE_STATE);
  SfbTzMapSetCommand (Map, 3, 0x203u, 0, SFB_TZ_SEMANTIC_WRITE_DEVICE_STATE);
  SfbTzMapSetCommand (Map, 4, 0x204u, 0, SFB_TZ_SEMANTIC_MILESTONE);
  SfbTzMapSetCommand (Map, 5, 0x207u, SFB_KM_SET_VERSION_BYTES, SFB_TZ_SEMANTIC_SET_VERSION);
  SfbTzMapSetCommand (Map, 6, 0x208u, SFB_KM_SET_BOOTSTATE_BYTES, SFB_TZ_SEMANTIC_SET_BOOTSTATE);
  SfbTzMapSetCommand (Map, 7, 0x211u, SFB_KM_SET_VBH_BYTES, SFB_TZ_SEMANTIC_SET_VBH);
  SfbTzMapSetCommand (Map, 8, 0x219u, 0, SFB_TZ_SEMANTIC_GENERATE_FRS_UDS);
}
