#include "HookCommon.h"
#include "SuperFbProfileRewrite.h"

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC SpssProtocol *gSpss = NULL;
STATIC SPSS_SHARE_KEYMINT_INFO gOrigShareKeyMintInfo = NULL;
STATIC BOOLEAN gSpssRewriteLogged = FALSE;
_Static_assert (sizeof (KeymintSharedInfoStruct) == 144,
                "KeymintSharedInfoStruct ABI size");
_Static_assert (offsetof (KeymintSharedInfoStruct, RootOfTrust) == 0,
                "KeymintSharedInfoStruct root-of-trust offset");
_Static_assert (offsetof (KeymintSharedInfoStruct, BootInfo) == 44,
                "KeymintSharedInfoStruct boot-info offset");
_Static_assert (offsetof (KeymintSharedInfoStruct, Vbh) == 108,
                "KeymintSharedInfoStruct VBH offset");
SFB_HOOK_GUARD_DEFINE (gSpssGuard);

STATIC EFI_STATUS EFIAPI
HookedShareKeyMintInfo (IN KeymintSharedInfoStruct *Info);

EFI_STATUS
SfbLocateSpss (OUT SpssProtocol **Protocol)
{
  EFI_STATUS Status;
  SpssProtocol *Spss = NULL;
  SPSS_SHARE_KEYMINT_INFO OrigShare = gOrigShareKeyMintInfo;

  if (Protocol == NULL) return EFI_INVALID_PARAMETER;
  *Protocol = NULL;
  gSpssRewriteLogged = FALSE;
  Status = gBS->LocateProtocol (&gEfiSPSSProtocolGuid, NULL, (VOID **)&Spss);
  if (EFI_ERROR (Status) || Spss == NULL) return EFI_NOT_FOUND;
  if (Spss->SPSSDxe_ShareKeyMintInfo == NULL) return EFI_NOT_READY;
  if (gSpss != NULL && gSpss != Spss) return EFI_NOT_READY;
  if (Spss->SPSSDxe_ShareKeyMintInfo == HookedShareKeyMintInfo) {
    if (OrigShare == NULL) return EFI_NOT_READY;
  } else if (OrigShare == NULL) {
    OrigShare = Spss->SPSSDxe_ShareKeyMintInfo;
  } else if (Spss->SPSSDxe_ShareKeyMintInfo != OrigShare) {
    return EFI_NOT_READY;
  }
  gOrigShareKeyMintInfo = OrigShare;
  gSpss = Spss;
  *Protocol = Spss;
  return EFI_SUCCESS;
}

EFI_STATUS
SfbInstallSpss (IN SpssProtocol *Protocol)
{
  if (Protocol == NULL || Protocol != gSpss || gOrigShareKeyMintInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Protocol->SPSSDxe_ShareKeyMintInfo != HookedShareKeyMintInfo &&
      Protocol->SPSSDxe_ShareKeyMintInfo != gOrigShareKeyMintInfo) {
    return EFI_NOT_READY;
  }
  if (Protocol->SPSSDxe_ShareKeyMintInfo != HookedShareKeyMintInfo) {
    Protocol->SPSSDxe_ShareKeyMintInfo = HookedShareKeyMintInfo;
  }
  return EFI_SUCCESS;
}
VOID
SfbRestoreSpss (VOID)
{
  if (gSpss != NULL &&
      gSpss->SPSSDxe_ShareKeyMintInfo == HookedShareKeyMintInfo) {
    gSpss->SPSSDxe_ShareKeyMintInfo = gOrigShareKeyMintInfo;
  }
  gSpss = NULL;
  gOrigShareKeyMintInfo = NULL;
  gSpssRewriteLogged = FALSE;
}


STATIC EFI_STATUS EFIAPI
HookedShareKeyMintInfo (IN KeymintSharedInfoStruct *Info)
{
  EFI_STATUS Status;
  BOOLEAN First = SfbHookEnter (&gSpssGuard);
  BOOLEAN Rewritten = FALSE;
  CONST SFB_MODE2_PROFILE *Profile;

  if (gOrigShareKeyMintInfo == NULL) {
    SfbHookLeave (&gSpssGuard);
    return EFI_NOT_READY;
  }
  Profile = SfbHooksProfile ();
  /* A wrapper can remain live while policy is false during arming/rollback;
   * this is an arm/disarm interlock, not a mode test. */
  if (First && SfbHooksActive () &&
      (UINT32)SfbHooksMode () == 2u && Profile != NULL && Info != NULL) {
    Rewritten = SfbRewriteSpss (
                  (UINT8 *)Info,
                  sizeof (KeymintSharedInfoStruct),
                  Profile);
    if (Rewritten && !gSpssRewriteLogged) {
      gSpssRewriteLogged = TRUE;
      DEBUG ((EFI_D_INFO,
              "SFB: MARK spss-rewrite bytes=%u\n",
              (UINT32)sizeof (KeymintSharedInfoStruct)));
    }
  }

  /* Reentrant calls and rejected/disabled transforms reach the real function
   * with the caller's original bytes. */
  Status = gOrigShareKeyMintInfo (Info);
  SfbHookLeave (&gSpssGuard);
  return Status;
}
