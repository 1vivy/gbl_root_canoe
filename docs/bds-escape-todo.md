# Deferred: one-shot Escape through the on-slot ABL

Escape is a proposal, not an implemented menu or supported launch path. The
ordinary Reboot menu uses the existing Qualcomm reset reason and Android misc
BCB conventions; it physically resets the phone.

A future Escape would extract the active on-slot ABL, temporarily suppress its
efisp dispatch and launch it within the current UEFI session. Current managed
loaders avoid recursion because the preparation patcher changes their UTF-16
`efisp` lookup to `nulls`. Current BDS has **no runtime efisp hiding hook**, and
neither that preparation patch nor a persisted escape flag is a ready-made
solution for running raw ABL.

Qualcomm reference ABL selects bootloader/recovery through
`EFI_RESETREASON_PROTOCOL.GetResetReason`, then clears that reason and reads misc
BCB. A scoped one-shot protocol override is a possible investigation path,
requiring OEM ABL qualification and restoration on every failed/returned child.
Fastbootd also needs a real `boot-fastboot` BCB command for recovery userspace;
a UEFI-only fake read does not survive kernel handoff. `RebootDevice` itself
calls ResetSystem and cannot be used as a no-reset target setter.

Qualification must establish active-ABL extraction, efisp recursion prevention,
child hook lifetime, reset/BCB precedence and returned-child cleanup together.
No automatic persistent boot instruction or new DeviceInfo projection is part
of this proposal. A future descriptor should state that Escape is a fallback,
that efisp cannot be flashed through that guarded path, and that storage may be
unavailable under the resulting Android boot state. Do not promise decryption
or stock-system restoration.
