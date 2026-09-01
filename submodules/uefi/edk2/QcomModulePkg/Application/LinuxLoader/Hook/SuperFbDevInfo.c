#include "SuperFbDevInfo.h"

#define SFB_DEVINFO_TEMPLATE \
  "unlocked=%a critical=%a waiver=%a retry_a=%a retry_b=%a"
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

SFB_BOOLEAN
SfbFormatObservedDevInfo (
  const SFB_OBSERVED_DEVINFO *Observed,
  const SFB_SLOT_RETRIES     *Retries,
  char                       *Buffer,
  SFB_UINTN                   BufferBytes
  )
{
  const char *Values[5];
  const char *Waiver;
  char        Unlocked[2] = { '0', '\0' };
  char        Critical[2] = { '0', '\0' };
  char        RetryA[2];
  char        RetryB[2];
  SFB_UINTN   Index;
  SFB_UINTN   ValueIndex = 0;
  SFB_DEVINFO_WRITER Writer;

  if (Buffer == NULL || BufferBytes == 0) {
    return FALSE;
  }
  Buffer[0] = '\0';

  if (Observed == NULL || Observed->Available == FALSE) {
    Waiver = "unknown";
  } else {
    Unlocked[0] = (Observed->Unlocked != FALSE) ? '1' : '0';
    Critical[0] = (Observed->Critical != FALSE) ? '1' : '0';
    /* Only a true-locked observation waives the first-install data format. */
    Waiver = (Observed->Unlocked == FALSE && Observed->Critical == FALSE)
                ? "1" : "0";
  }

  Values[0] = Unlocked;
  Values[1] = Critical;
  Values[2] = Waiver;
  Values[3] = SfbRetryValue (Retries == NULL ? NULL : &Retries->A, RetryA);
  Values[4] = SfbRetryValue (Retries == NULL ? NULL : &Retries->B, RetryB);
  Writer.Buffer = Buffer;
  Writer.Capacity = BufferBytes;
  Writer.Length = 0;

  for (Index = 0; SFB_DEVINFO_TEMPLATE[Index] != '\0'; ++Index) {
    char Literal[2] = { SFB_DEVINFO_TEMPLATE[Index], '\0' };

    if (Literal[0] == '%' && SFB_DEVINFO_TEMPLATE[Index + 1u] == 'a') {
      if (ValueIndex >= 5u ||
          !SfbAppendDevInfoValue (&Writer, Values[ValueIndex++])) {
        return FALSE;
      }
      ++Index;
    } else if (!SfbAppendDevInfoValue (&Writer, Literal)) {
      return FALSE;
    }
  }
  return (SFB_BOOLEAN)(ValueIndex == 5u);
}
