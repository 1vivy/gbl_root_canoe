# Xiaomi & OnePlus System Update Security Warning

## Universal SCM safeguards (all modes)

All modes (0/1/2) best-effort suppress the TrustZone fuse and anti-rollback SCM requests during launch and OTA refresh. This prevents **further advancement only**: it cannot un-blow an already-blown fuse or lower an already-raised rollback floor. If the SCM protocol is absent, launch continues and the `hooks-armed ... scm=0` marker records that the safeguard was unavailable.

## Xiaomi

Currently, Xiaomi fixed the GBL vulnerability in version **300**, but as of version **306**, XBL retains the ability to boot an old version of abl to indirectly load EFISP.

**OTA method：**
Nothing is flashed automatically. After an OTA package is installed — and before rebooting — open the module WebUI (or use the manual toolkit flow) to explicitly refresh the patched ABL/profile/map set and `BDS.efi`. Regenerate `boot.efi.gm2p` from the matching stock vbmeta and the optional `boot.efi.tzmap` from the unpatched ABL. Continued booting depends on the new build's abl anti-rollback version remaining unchanged.

**⚠️ Critical risk：**
If the abl avb version changes, this method will cause a **hard brick**.

**Recommendation：**
- Use **Hail** to freeze system updates
- **Do NOT update unless necessary / on a primary device**
- **Do NOT update unless necessary / on a primary device**
- **Do NOT update unless necessary / on a primary device**
- **Do NOT update unless necessary / on a primary device**
- If you must update, ensure you check abl's anti-rollback version, or wait for others to have tested it

**Version info：**
- Fixed abl version (loading efisp)：OS3.0.300
- Highest version successfully updated using the module in testing：3.0.306


## OnePlus

Newer builds fix the loader path, so an older vulnerable ABL stays on the `abl` partition; given the previous "fuse" incident, it is still recommended to use Hail to freeze system updates.

**⚠️ Warnings：**
- **Do NOT update unless necessary / on a primary device**
- **Do NOT update unless necessary / on a primary device**
- **Do NOT update unless necessary / on a primary device**
- **Do NOT update unless necessary / on a primary device**
- If you must update, ensure you check abl's anti-rollback version, wait for others to have tested it, or confirm that the new version still has the GBL vulnerability
- You can also use the module for OTA

**Version info：**
- Vulnerable through `16.0.5.7xx` and below; newer builds are fixed, so keep a vulnerable ABL for the `abl` partition and let the patched loader track your firmware.


## Regarding future fuse of abl anti-rollback versions

If abl anti-rollback versions are really being fused in the future, it is recommended to abandon updates entirely, or only update **HLOS**.

### How to extract HLOS

1. Extract `payload.bin` to the `images` directory
2. Use the following script to check if files in the `images` directory contain the `AVB0` header. If yes, it is considered an HLOS image； otherwise non-HLOS.

```python
#!/usr/bin/env python3
img_dir = "./images"
import os
for img in os.listdir(img_dir):
    with open(os.path.join(img_dir, img), "rb") as f:
        if b"AVB0" in f.read():
            print(f"{img} is an hlos image")
```

3. Flash these partitions using fastboot


## About the module

The module installer and WebUI support both Chinese and English.
