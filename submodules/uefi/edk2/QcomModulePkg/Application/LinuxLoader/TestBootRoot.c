#include "SuperFbBootRoot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  SFB_BOOT_ROOT_STATE State;
  const char         *Value;
  SFB_BOOLEAN         IsEmpty;
} BOOT_ROOT_CASE;

int
main (void)
{
  static const BOOT_ROOT_CASE Cases[] = {
    { SfbBootRootPopulatedConfig, "populated-config", FALSE },
    { SfbBootRootPopulatedManaged, "populated-managed", FALSE },
    { SfbBootRootEmptyRoot, "empty-root", TRUE },
    { SfbBootRootNoRoot, "no-root", TRUE },
    { SfbBootRootNoVolumes, "no-volumes", TRUE }
  };
  char Value[SFB_BOOT_ROOT_VALUE_BYTES];
  char TooSmall[sizeof ("populated-managed") - 1u];
  size_t Index;

  assert(SFB_BOOT_ROOT_VALUE_BYTES >= sizeof ("populated-managed"));
  for (Index = 0; Index < sizeof (Cases) / sizeof (Cases[0]); ++Index) {
    assert(SfbBootRootFormat(Cases[Index].State, Value, sizeof (Value)));
    assert(strcmp(Value, Cases[Index].Value) == 0);
    assert(SfbBootRootIsEmptyState(Cases[Index].State) ==
           Cases[Index].IsEmpty);
  }

  memset(TooSmall, 'x', sizeof (TooSmall));
  assert(!SfbBootRootFormat(SfbBootRootPopulatedManaged, TooSmall,
                            sizeof (TooSmall)));
  for (Index = 0; Index < sizeof (TooSmall); ++Index) {
    assert(TooSmall[Index] == 'x');
  }

  puts("boot root: five states and empty-state truth table");
  return 0;
}
