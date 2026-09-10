#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbDevInfo.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main (void)
{
  SFB_OBSERVED_DEVINFO Unknown = { FALSE, FALSE, FALSE };
  SFB_OBSERVED_DEVINFO Locked = { TRUE, FALSE, FALSE };
  SFB_OBSERVED_DEVINFO Unlocked = { TRUE, TRUE, TRUE };
  SFB_SLOT_RETRIES MissingB = {
    { TRUE, 3, FALSE, FALSE },
    { FALSE, 0, FALSE, FALSE }
  };
  SFB_SLOT_RETRIES Known = {
    { TRUE, 3, FALSE, FALSE },
    { TRUE, 7, TRUE, FALSE }
  };
  char Value[SFB_DEVINFO_VALUE_BYTES];
  char RetryValue[SFB_SLOT_RETRIES_VALUE_BYTES];
  unsigned Flags;

  assert(SfbFormatObservedDevInfo(&Unknown, Value, sizeof(Value)));
  assert(strcmp(Value, "unlocked=unknown critical=unknown waiver=unknown") == 0);
  assert(SfbFormatSlotRetries(&MissingB, RetryValue, sizeof(RetryValue)));
  assert(strcmp(RetryValue, "retry_a=3 retry_b=unknown") == 0);
  puts("missing abl_b: retry_b=unknown (no numeric retry emitted)");

  assert(SfbFormatObservedDevInfo(&Locked, Value, sizeof(Value)));
  assert(strcmp(Value,
                "unlocked=0 critical=0 waiver=1") == 0);
  assert(SfbFormatObservedDevInfo(&Unlocked, Value, sizeof(Value)));
  assert(strcmp(Value,
                "unlocked=1 critical=1 waiver=0") == 0);
  assert(SfbFormatSlotRetries(&Known, RetryValue, sizeof(RetryValue)));
  assert(strcmp(RetryValue, "retry_a=3 retry_b=7") == 0);
  puts("known devinfo: locked waiver=1; separate slot retry values");

  Known.B.RetryCount = 8;
  assert(SfbFormatObservedDevInfo(&Locked, Value, sizeof(Value)));
  assert(SfbFormatSlotRetries(&Known, RetryValue, sizeof(RetryValue)));
  assert(strstr(RetryValue, "retry_b=unknown") != NULL);
  puts("malformed retry_b=8: retry_b=unknown (not clamped/defaulted)");
  for (Flags = 0; Flags < 8; ++Flags) {
    SFB_OBSERVED_DEVINFO State = { (Flags & 1) != 0, (Flags & 2) != 0, (Flags & 4) != 0 };
    unsigned Length;
    assert(SfbFormatObservedDevInfo(&State, Value, sizeof(Value)));
    Length = (unsigned)strlen(Value);
    assert(Length <= 60);
    if (!State.Available)
      assert(strcmp(Value, "unlocked=unknown critical=unknown waiver=unknown") == 0);
    else if (!State.Unlocked && !State.Critical)
      assert(strcmp(Value, "unlocked=0 critical=0 waiver=1") == 0);
    else assert(strstr(Value, "waiver=0") != NULL);
    assert(SfbFormatObservedDevInfo(&State, Value, Length + 1));
    assert(!SfbFormatObservedDevInfo(&State, Value, Length));
    assert(Value[0] == '\0');
  }
  assert(SfbFormatObservedDevInfo(NULL, Value, sizeof(Value)));
  assert(strcmp(Value, "unlocked=unknown critical=unknown waiver=unknown") == 0);
  assert(!SfbFormatObservedDevInfo(NULL, NULL, 0));
  assert(SfbFormatSlotRetries(NULL, RetryValue, sizeof(RetryValue)));
  assert(strcmp(RetryValue, "retry_a=unknown retry_b=unknown") == 0);
  assert(strlen(RetryValue) <= 60);
  assert(!SfbFormatSlotRetries(NULL, RetryValue, strlen(RetryValue)));
  assert(RetryValue[0] == '\0');
  puts("all observer states remain distinct and fit a complete direct fastboot reply");
  return 0;
}
