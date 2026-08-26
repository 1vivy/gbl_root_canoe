# Xiaomi & OnePlus System Update Security Warning

## The 7.x OTA watcher

The installed device module now includes a background `service.sh` watcher. It watches the `abl_a` and `abl_b` device nodes with `inotifyd`, checks a candidate change against the SHA-256 digest recorded at install, and falls back to slow polling when `inotifyd` is unavailable. An OTA normally changes the inactive slot; when the ABL really changes, the watcher re-derives that slot's pair and adds a new entry to `canoe.cfg` with the right role.

The watcher leaves the entry that currently boots in place. The previously working entry is never removed. You do not need to reopen the WebUI and flash again after every OTA. If you want to change a mode, the WebUI selector still works, but it names and rewrites a `canoe.cfg` entry rather than a partition record.

## Universal SCM safeguards (all modes)

All modes (0/1/2) best-effort suppress the TrustZone fuse and anti-rollback SCM requests during launch and OTA refresh. This prevents **further advancement only**: it cannot un-blow an already-blown fuse or lower an already-raised rollback floor. If the SCM protocol is absent, launch continues and the `hooks-armed ... scm=0` marker records that the safeguard was unavailable.

## Xiaomi

Currently, Xiaomi fixed the GBL vulnerability in version **300**, but as of version **306**, XBL retains the ability to boot an old version of ABL to indirectly load `efisp`.

**OTA method:**

The watcher handles the ABL change on the inactive slot and adds a matching entry to `canoe.cfg`; it does not remove the entry that currently boots. Nothing needs to be flashed manually after every OTA. Keep the module installed so its watcher can run, and use the WebUI only when you intentionally want to change the mode of a named entry. The patched loader and `.gm2p` sidecar still have to describe the same stock firmware pair.

**Critical risk:**

If the ABL AVB version changes, this method can cause a **hard brick**. The watcher does not make an already-incompatible vendor ABL safe.

**Recommendation:**

- Use **Hail** to freeze system updates.
- **Do NOT update unless necessary / on a primary device.**
- If you must update, ensure you check ABL's anti-rollback version, or wait for others to have tested it.

**Version info:**

- Fixed ABL version (loading `efisp`): OS3.0.300
- Highest version successfully updated using the module in testing: 3.0.306

## OnePlus

Newer builds fix the loader path, so an older vulnerable ABL stays on the `abl` partition; given the previous fuse incident, it is still recommended to use Hail to freeze system updates.

**Warnings:**

- **Do NOT update unless necessary / on a primary device.**
- If you must update, ensure you check ABL's anti-rollback version, wait for others to have tested it, or confirm that the new version still has the GBL vulnerability.
- The module watcher can add the inactive-slot entry after an OTA; it does not remove the entry that currently boots.

**Version info:**

- Vulnerable through `16.0.5.7xx` and below; newer builds are fixed, so keep a vulnerable ABL for the `abl` partition and let the patched loader track your firmware.

## Regarding future fuse of ABL anti-rollback versions

If ABL anti-rollback versions are really being fused in the future, it is recommended to abandon updates entirely, or only update **HLOS**.

### How to extract HLOS

1. Extract `payload.bin` to the `images` directory.
2. Use the following script to check if files in the `images` directory contain the `AVB0` header. If yes, it is considered an HLOS image; otherwise non-HLOS.

```python
#!/usr/bin/env python3
img_dir = "./images"
import os
for img in os.listdir(img_dir):
    with open(os.path.join(img_dir, img), "rb") as f:
        if b"AVB0" in f.read():
            print(f"{img} is an hlos image")
```

3. Flash these partitions using fastboot.

## Boot failure symptoms

| Symptom | Likely cause | Recovery |
|---------|--------------|----------|
| Black screen after the vendor logo, no fastboot text; the host sees `QUSB_BULK_CID` (EDL 9008) | Pre-ABL failure: the ABL on the active slot cannot run — e.g. a foreign ABL was flashed, or the bootloader swapped onto a slot the module never paired | Authorized EDL flash of the matching current firmware |
| Bootloader and recovery still work, but the system will not boot | Mismatched or half-installed chain: the BDS in `efisp` and the sidecar set in `persist` come from different generations | Re-run the full install or toolkit staging flow as one unit, so `BDS.efi`, `boot.efi`, `.gm2p` and `.tzmap` are all from one generation |
| Red screen | Verified-boot state refused by the firmware | Boot into recovery and refresh or remove the chain |

## About the module

The module installer and WebUI support both Chinese and English.
