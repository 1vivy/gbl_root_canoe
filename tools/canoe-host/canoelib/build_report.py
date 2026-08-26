"""The bilingual operator report for the `canoe build` subcommand.

Text only. It lives apart from the pipeline so the pipeline stays reviewable and
so a wording or translation change never touches derive logic.
"""

from __future__ import annotations

from typing import Final

_OUTPUTS: Final = """
========================================
Patched. Outputs:
  efisp/boot.efi       - patched ABL loader (use with the matching GM2P profile)
  efisp/boot.efi.gm2p  - locked/green KeyMint profile for images/vbmeta.img
  efisp/boot.efi.tzmap - ABL-derived TrustZone interface map
  efisp/tools/         - EFI tools (Reboot / BL / ARB tools), available from the built-in EFI Tools menu row
  BDS.efi              - superfastboot BDS (flash to efisp with fastboot)
  ABL_original.efi     - original unpatched loader (for analysis; do NOT flash)

---- Install the boot root over ADB or USB Mass Storage ----
Standalone, no firmware package:
     ./canoe prep-device    # derive from the device's own abl/vbmeta
     ./canoe install        # install the shared persist tree and canoe.cfg
Alongside a Super Flasher / RegionalHybrid package:
     ./canoe prep --pkg <dir> --recovery <custom>.img \\
                  --abl <vulnerable>.img --in-place
     # run the package's own flasher, then:
     ./canoe install
See README.canoe.md for the full contract and guarantees.
"""


_GBL_MISSING: Final = """
WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL).
efisp/boot.efi is still produced and valid, but the abl partition must be
downgraded to an older ABL with the GBL vulnerability before booting.
警告：此 ABL 中未找到 GBL 漏洞（Failed to patch ABL GBL）。
efisp/boot.efi 仍已生成且有效，但开机前必须将 abl 分区降级为带 GBL 漏洞的旧版 ABL。
"""

_FLASH_BDS_EN: Final = """Flash the bootloader bundle from the host:
     fastboot flash abl <vulnerable>.img   (only when the installed ABL lacks the GBL vulnerability)
     fastboot flash efisp BDS.efi"""

_FLASH_BDS_ZH: Final = """在主机上刷入引导加载程序套件：
     fastboot flash abl <vulnerable>.img   （仅当已安装的 ABL 不包含 GBL 漏洞时）
     fastboot flash efisp BDS.efi"""

_DOWNGRADE_EN: Final = """Downgrade the abl partition to an older ABL with the GBL vulnerability
   (efisp/boot.efi and the abl partition do not need to match versions)"""

_DOWNGRADE_ZH: Final = """将 abl 分区降级为带 GBL 漏洞的旧版 ABL
   （efisp/boot.efi 与 abl 分区版本不必一致）"""

_MANUAL_EN: Final = """
---- Manual install flow (English) ----
1. Copy the efisp/ folder to the persist boot root:
     cp -r efisp/. /mnt/vendor/persist/efisp/
   (from a custom recovery the boot root is /persist/efisp instead;
    create the directory first if needed, e.g. via MT Manager)
2. sync
"""

_MANUAL_ZH: Final = """
---- 手动安装步骤 (中文) ----
1. 将 efisp/ 文件夹复制到 persist 启动根目录：
     cp -r efisp/. /mnt/vendor/persist/efisp/
   （如不存在请先创建 /mnt/vendor/persist/efisp，例如用 MT 管理器）
2. sync
"""


def _numbered_steps(*, gbl_patched: bool, downgrade: str, flash: str) -> str:
    """The tail of a manual flow, which grows a step when the ABL needs downgrading."""
    if gbl_patched:
        return f"3. {flash}"
    return f"3. {downgrade}\n4. {flash}"


def build_report(*, gbl_patched: bool) -> str:
    """The full report for a successful derive."""
    sections = [_OUTPUTS]
    if not gbl_patched:
        sections.append(_GBL_MISSING)
    for manual, downgrade, flash in (
        (_MANUAL_EN, _DOWNGRADE_EN, _FLASH_BDS_EN),
        (_MANUAL_ZH, _DOWNGRADE_ZH, _FLASH_BDS_ZH),
    ):
        steps = _numbered_steps(gbl_patched=gbl_patched, downgrade=downgrade, flash=flash)
        sections.append(manual + steps)
    sections.append("========================================")
    return "\n".join(sections)
