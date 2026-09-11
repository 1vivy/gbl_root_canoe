/* Exercise production framing, including the long values that previously crashed getvar. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include "../edk2/QcomModulePkg/Library/FastbootLib/FastbootResponse.h"
#include "../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbDevInfo.h"

static char Collected[4096];

static VOID
Collect (CONST CHAR8 *Payload)
{
  struct { char Before; char Data[65]; char After; } Frame = {0};
  UINTN Bytes;
  Frame.Before = 'L'; Frame.After = 'R';
  Bytes = SfbFastbootEncodeResponse("INFO", Payload, Frame.Data);
  assert(Bytes >= 5 && Bytes <= 64);
  assert(Bytes == strlen(Frame.Data));
  assert(Frame.Before == 'L' && Frame.After == 'R');
  assert(memcmp(Frame.Data, "INFO", 4) == 0);
  assert(strlen(Payload) <= 60);
  strcat(Collected, Frame.Data + 4);
}

static void
CheckVariable (const char *Name, const char *Value)
{
  char Expected[4096];
  snprintf(Expected, sizeof(Expected), "%s:%s", Name, Value);
  Collected[0] = '\0';
  SfbFastbootGetVarInfo(Name, Value, Collect);
  assert(strcmp(Collected, Expected) == 0);
}

int main (void)
{
  SFB_OBSERVED_DEVINFO Known = {TRUE, TRUE, TRUE};
  SFB_OBSERVED_DEVINFO Unknown = {FALSE, FALSE, FALSE};
  SFB_SLOT_RETRIES Retries = {{TRUE, 7, FALSE, FALSE}, {TRUE, 7, FALSE, FALSE}};
  char Name[200], Value[500], Frame[65];
  char DevInfo[SFB_DEVINFO_VALUE_BYTES], RetryValue[SFB_SLOT_RETRIES_VALUE_BYTES];
  unsigned Index;

  CheckVariable("canoe-devinfo", "unlocked=1 critical=1 waiver=0 retry_a=7 retry_b=7");
  CheckVariable("canoe-devinfo", "unlocked=1 critical=1 waiver=0 retry_a=unknown retry_b=unknown");
  CheckVariable("canoe-devinfo", "unlocked=0 critical=0 waiver=unknown retry_a=unknown retry_b=unknown");
  assert(SfbFormatObservedDevInfo(&Known, DevInfo, sizeof(DevInfo)));
  CheckVariable("canoe-devinfo", DevInfo);
  assert(SfbFastbootEncodeResponse("OKAY", DevInfo, Frame) == strlen(DevInfo) + 4);
  assert(strcmp(Frame + 4, DevInfo) == 0);
  assert(SfbFormatObservedDevInfo(&Unknown, DevInfo, sizeof(DevInfo)));
  CheckVariable("canoe-devinfo", DevInfo);
  assert(SfbFastbootEncodeResponse("OKAY", DevInfo, Frame) == strlen(DevInfo) + 4);
  assert(strcmp(Frame + 4, DevInfo) == 0);
  assert(SfbFormatSlotRetries(&Retries, RetryValue, sizeof(RetryValue)));
  CheckVariable("canoe-slot-retries", RetryValue);
  assert(SfbFormatSlotRetries(NULL, RetryValue, sizeof(RetryValue)));
  CheckVariable("canoe-slot-retries", RetryValue);
  assert(SfbFastbootEncodeResponse("OKAY", RetryValue, Frame) == strlen(RetryValue) + 4);
  assert(strcmp(Frame + 4, RetryValue) == 0);
  CheckVariable("canoe-boot-root", "empty-root");
  CheckVariable("", "");
  for (Index = 0; Index < sizeof(Name); ++Index) {
    memset(Name, 'n', Index); Name[Index] = '\0';
    memset(Value, 'v', sizeof(Value) - 1); Value[sizeof(Value) - 1] = '\0';
    CheckVariable(Name, Value);
    Value[0] = '\0'; CheckVariable(Name, Value);
  }
  assert(SfbFastbootEncodeResponse("OKAY", NULL, Frame) == 4);
  assert(strcmp(Frame, "OKAY") == 0);
  memset(Value, 'v', 60); Value[60] = '\0';
  assert(SfbFastbootEncodeResponse("OKAY", Value, Frame) == 64);
  assert(strlen(Frame) == 64 && strcmp(Frame + 4, Value) == 0);
  memset(Value, 'v', 61); Value[61] = '\0';
  assert(SfbFastbootEncodeResponse("FAIL", Value, Frame) == 64);
  assert(Frame[64] == '\0');
  puts("PASS fastboot framing: complete INFO streams, 64-byte packets, empty OKAY, boundaries");
  return 0;
}
