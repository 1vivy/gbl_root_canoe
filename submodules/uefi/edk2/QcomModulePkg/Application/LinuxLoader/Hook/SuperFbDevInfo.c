#include "SuperFbDevInfo.h"

#define SFB_DEVINFO_TEMPLATE "unlocked=%a critical=%a waiver=%a"
#define SFB_SLOT_RETRIES_TEMPLATE "retry_a=%a retry_b=%a"
#define SFB_RETRY_UNKNOWN "unknown"

typedef struct {
  char      *Buffer;
  SFB_UINTN  Capacity;
  SFB_UINTN  Length;
} SFB_DEVINFO_WRITER;

static SFB_BOOLEAN
SfbAppendDevInfoValue (SFB_DEVINFO_WRITER *Writer, const char *Value)
{
  SFB_UINTN Index;

  if (Writer == NULL || Value == NULL) {
    return FALSE;
  }
  for (Index = 0; Value[Index] != '\0'; ++Index) {
    if (Writer->Length + 1u >= Writer->Capacity) {
      Writer->Buffer[0] = '\0';
      return FALSE;
    }
    Writer->Buffer[Writer->Length++] = Value[Index];
  }
  Writer->Buffer[Writer->Length] = '\0';
  return TRUE;
}

static const char *
SfbRetryValue (
  const SFB_SLOT_RETRY_STATE *Retry,
  char                       *Digit
  )
{
  if (Retry == NULL || Retry->Available == FALSE || Retry->RetryCount > 7u) {
    return SFB_RETRY_UNKNOWN;
  }
  Digit[0] = (char)('0' + Retry->RetryCount);
  Digit[1] = '\0';
  return Digit;
}

static SFB_BOOLEAN
SfbFormatFields (
  const char *Template, const char *const *Values, SFB_UINTN ValueCount,
  char *Buffer, SFB_UINTN BufferBytes)
{
  SFB_UINTN Index;
  SFB_UINTN ValueIndex = 0;
  SFB_DEVINFO_WRITER Writer;

  if (Buffer == NULL || BufferBytes == 0) return FALSE;
  Buffer[0] = '\0';
  Writer.Buffer = Buffer;
  Writer.Capacity = BufferBytes;
  Writer.Length = 0;
  for (Index = 0; Template[Index] != '\0'; ++Index) {
    char Literal[2] = { Template[Index], '\0' };
    if (Literal[0] == '%' && Template[Index + 1u] == 'a') {
      if (ValueIndex >= ValueCount ||
          !SfbAppendDevInfoValue (&Writer, Values[ValueIndex++])) return FALSE;
      ++Index;
    } else if (!SfbAppendDevInfoValue (&Writer, Literal)) return FALSE;
  }
  return (SFB_BOOLEAN)(ValueIndex == ValueCount);
}

SFB_BOOLEAN
SfbFormatObservedDevInfo (
  const SFB_OBSERVED_DEVINFO *Observed,
  char *Buffer, SFB_UINTN BufferBytes)
{
  const char *Values[3] = { "unknown", "unknown", "unknown" };
  char Unlocked[2];
  char Critical[2];

  if (Observed != NULL && Observed->Available != FALSE) {
    Unlocked[0] = (Observed->Unlocked != FALSE) ? '1' : '0';
    Critical[0] = (Observed->Critical != FALSE) ? '1' : '0';
    Unlocked[1] = Critical[1] = '\0';
    Values[0] = Unlocked;
    Values[1] = Critical;
    /* Only an actual pre-repair locked observation qualifies. Unknown is
     * neither locked nor unlocked and must not be serialized as zero. */
    Values[2] = (Observed->Unlocked == FALSE && Observed->Critical == FALSE)
                  ? "1" : "0";
  }
  return SfbFormatFields (SFB_DEVINFO_TEMPLATE, Values, 3, Buffer, BufferBytes);
}

SFB_BOOLEAN
SfbFormatSlotRetries (
  const SFB_SLOT_RETRIES *Retries, char *Buffer, SFB_UINTN BufferBytes)
{
  char RetryA[2];
  char RetryB[2];
  const char *Values[2];
  Values[0] = SfbRetryValue (Retries == NULL ? NULL : &Retries->A, RetryA);
  Values[1] = SfbRetryValue (Retries == NULL ? NULL : &Retries->B, RetryB);
  return SfbFormatFields (SFB_SLOT_RETRIES_TEMPLATE, Values, 2, Buffer, BufferBytes);
}
