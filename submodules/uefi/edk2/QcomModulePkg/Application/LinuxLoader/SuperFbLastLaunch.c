#include "SuperFbLastLaunch.h"

typedef struct {
  char      *Buffer;
  SFB_UINTN  Capacity;
  SFB_UINTN  Length;
} SFB_LAST_LAUNCH_WRITER;

static SFB_BOOLEAN
SfbLastLaunchAppend (SFB_LAST_LAUNCH_WRITER *Writer, const char *Text)
{
  SFB_UINTN Index;

  if (Writer == NULL || Text == NULL) return FALSE;
  for (Index = 0; Text[Index] != '\0'; ++Index) {
    if (Writer->Length + 1u >= Writer->Capacity) return FALSE;
    Writer->Buffer[Writer->Length++] = Text[Index];
  }
  Writer->Buffer[Writer->Length] = '\0';
  return TRUE;
}

static SFB_BOOLEAN
SfbLastLaunchConsume (const char *Value, SFB_UINTN ValueBytes,
                      SFB_UINTN *Offset, const char *Expected)
{
  SFB_UINTN Index;

  if (Value == NULL || Offset == NULL || Expected == NULL) return FALSE;
  for (Index = 0; Expected[Index] != '\0'; ++Index) {
    if (*Offset >= ValueBytes || Value[*Offset] != Expected[Index]) {
      return FALSE;
    }
    (*Offset)++;
  }
  return TRUE;
}

SFB_BOOLEAN
SfbLastLaunchFormat (
  const SFB_LAST_LAUNCH *Launch,
  char                  *Buffer,
  SFB_UINTN              BufferBytes
  )
{
  static const char *const Reasons[] = {
    "none", "profile-absent", "lockstate-refused"
  };
  SFB_LAST_LAUNCH_WRITER Writer;
  char Requested[2];
  char Effective[2];

  if (Buffer == NULL || BufferBytes == 0) return FALSE;
  Buffer[0] = '\0';
  if (Launch == NULL || Launch->Requested > 2u || Launch->Effective > 2u ||
      (SFB_UINT32)Launch->Reason > (SFB_UINT32)SfbLaunchReasonLockstateRefused) {
    return FALSE;
  }
  Requested[0] = (char)('0' + Launch->Requested);
  Requested[1] = '\0';
  Effective[0] = (char)('0' + Launch->Effective);
  Effective[1] = '\0';
  Writer.Buffer = Buffer;
  Writer.Capacity = BufferBytes;
  Writer.Length = 0;
  return SfbLastLaunchAppend (&Writer, "requested=") &&
         SfbLastLaunchAppend (&Writer, Requested) &&
         SfbLastLaunchAppend (&Writer, " effective=") &&
         SfbLastLaunchAppend (&Writer, Effective) &&
         SfbLastLaunchAppend (&Writer, " reason=") &&
         SfbLastLaunchAppend (&Writer, Reasons[Launch->Reason]);
}

SFB_BOOLEAN
SfbLastLaunchParse (
  const char      *Value,
  SFB_UINTN        ValueBytes,
  SFB_LAST_LAUNCH *Launch
  )
{
  SFB_UINTN Offset = 0;
  SFB_UINTN ReasonOffset;
  SFB_LAST_LAUNCH Parsed;

  if (Value == NULL || Launch == NULL ||
      !SfbLastLaunchConsume (Value, ValueBytes, &Offset, "requested=") ||
      Offset >= ValueBytes || Value[Offset] < '0' || Value[Offset] > '2') {
    return FALSE;
  }
  Parsed.Requested = (SFB_UINT32)(Value[Offset++] - '0');
  if (!SfbLastLaunchConsume (Value, ValueBytes, &Offset, " effective=") ||
      Offset >= ValueBytes || Value[Offset] < '0' || Value[Offset] > '2') {
    return FALSE;
  }
  Parsed.Effective = (SFB_UINT32)(Value[Offset++] - '0');
  if (!SfbLastLaunchConsume (Value, ValueBytes, &Offset, " reason=")) {
    return FALSE;
  }
  ReasonOffset = Offset;
  if (SfbLastLaunchConsume (Value, ValueBytes, &ReasonOffset, "none")) {
    Parsed.Reason = SfbLaunchReasonNone;
  } else {
    ReasonOffset = Offset;
    if (SfbLastLaunchConsume (Value, ValueBytes, &ReasonOffset,
                              "profile-absent")) {
      Parsed.Reason = SfbLaunchReasonProfileAbsent;
    } else {
      ReasonOffset = Offset;
      if (!SfbLastLaunchConsume (Value, ValueBytes, &ReasonOffset,
                                 "lockstate-refused")) {
        return FALSE;
      }
      Parsed.Reason = SfbLaunchReasonLockstateRefused;
    }
  }
  Offset = ReasonOffset;
  if (Offset != ValueBytes) return FALSE;
  *Launch = Parsed;
  return TRUE;
}
