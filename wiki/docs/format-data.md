# Format phone data

The manager's native userdata evaluator is shared by desktop, WebUI and the
KSU installer. It considers the **effective boot presentation and identity**,
not whether a vbmeta file changed byte-for-byte. Module installation alone is
independent of that transition.

| Scenario | Assessment |
| --- | --- |
| Install/update manager only | Not needed |
| Update BDS/tools, retaining mode and identity | Not needed |
| First deployment from genuinely unlocked Android to Mode 1/2 | Required |
| Truly locked DeviceInfo recorded before Canoe repair, first boot into Mode 1/2 with compatible identity | Not needed |
| First deployment without reliable original state | Unknown |
| Mode 1 ↔ Mode 2, preserving effective signer and compatible versions | Not needed |
| Normal Mode 1/2 OTA with the same effective signer and newer versions | Not normally needed |
| Custom-ROM Mode 2 update retaining its effective spoof identity and compatible versions | Not normally needed |
| Change to a different effective signing identity | Required to adopt the changed binding |
| Mode 0 ↔ Mode 1/2 | Required |
| Same signer with a relevant version/patch downgrade | May be needed; return to a compatible version when possible |
| Invalid graft, rejected recovery, or incompatible AVB chain | Formatting is not the remedy |
| Manual A/B reconciliation or Both slots | Assess each resulting boot state |

Mode 2's effective identity comes from its installed profile; it need not be
identical to the current ROM's signing identity. Mode 1 uses the verified boot
identity. Equal public-key digests can relate changed vbmeta generations;
changed bytes or signatures alone are expected during an OTA. A matching key
neither proves firmware suitability nor waives version/patch compatibility.

BDS records launch evidence in logfs. Where attribution needs user confirmation,
confirm the relevant launch record in the app. Host installation history must
be captured before initial provisioning: flashing raw efisp destroys the
opportunity to identify what previously occupied that partition. Missing
information remains Unknown and is not a recommendation to format.

Canoe does not format automatically. Rebooting to recovery is separate from
choosing Format data there. Returning to the previous compatible boot state can
make existing data readable again; formatting permanently discards it.
