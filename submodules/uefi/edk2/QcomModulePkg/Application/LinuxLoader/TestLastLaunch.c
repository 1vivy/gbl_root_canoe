#include "SuperFbLastLaunch.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main (void)
{
  SFB_LAST_LAUNCH Input = { 2, 0, SfbLaunchReasonLockstateRefused };
  SFB_LAST_LAUNCH Parsed;
  char Value[SFB_LAST_LAUNCH_VALUE_BYTES];

  assert(SfbLastLaunchFormat(&Input, Value, sizeof(Value)));
  assert(strcmp(Value,
                "requested=2 effective=0 reason=lockstate-refused") == 0);
  assert(SfbLastLaunchParse(Value, strlen(Value), &Parsed));
  assert(Parsed.Requested == 2 && Parsed.Effective == 0);
  assert(Parsed.Reason == SfbLaunchReasonLockstateRefused);
  puts("last launch: requested=2 effective=0 reason=lockstate-refused");
  return 0;
}
