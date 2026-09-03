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

  assert(SfbFormatObservedDevInfo(&Unknown, &MissingB, Value, sizeof(Value)));
  assert(strcmp(Value, "unlocked=0 critical=0 waiver=unknown retry_a=3 retry_b=unknown") == 0);
  assert(strstr(Value, "retry_b=0") == NULL);
  assert(strstr(Value, "retry_b=7") == NULL);
  puts("missing abl_b: retry_b=unknown (no numeric retry emitted)");

  assert(SfbFormatObservedDevInfo(&Locked, &Known, Value, sizeof(Value)));
  assert(strcmp(Value,
                "unlocked=0 critical=0 waiver=1 retry_a=3 retry_b=7") == 0);
  assert(SfbFormatObservedDevInfo(&Unlocked, &Known, Value, sizeof(Value)));
  assert(strcmp(Value,
                "unlocked=1 critical=1 waiver=0 retry_a=3 retry_b=7") == 0);
  puts("known devinfo: locked waiver=1; retry_a=3 retry_b=7");

  Known.B.RetryCount = 8;
  assert(SfbFormatObservedDevInfo(&Locked, &Known, Value, sizeof(Value)));
  assert(strstr(Value, "retry_b=unknown") != NULL);
  puts("malformed retry_b=8: retry_b=unknown (not clamped/defaulted)");
  return 0;
}
