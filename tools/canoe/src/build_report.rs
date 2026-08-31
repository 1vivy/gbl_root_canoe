const OUTPUTS: &str = "
========================================
Patched. Outputs:
  efisp/boot.efi       - patched ABL loader (use with the matching GM2P profile)
  efisp/boot.efi.gm2p  - locked/green KeyMint profile for images/vbmeta.img
  efisp/boot.efi.tzmap - ABL-derived TrustZone interface map
  efisp/tools/         - EFI tools (Reboot / BL / ARB tools), available from the built-in
                          EFI Tools menu row
  BDS.efi              - superfastboot BDS (flash to efisp with fastboot)
  ABL_original.efi     - original unpatched loader (for analysis; do NOT flash)

";

const GBL_MISSING: &str = "
WARNING: No GBL exploit found in this ABL (Failed to patch ABL GBL).
efisp/boot.efi is still produced and valid, but the abl partition must be
downgraded to an older ABL with the GBL vulnerability before booting.
警告：此 ABL 中未找到 GBL 漏洞（Failed to patch ABL GBL）。
efisp/boot.efi 仍已生成且有效，但开机前必须将 abl 分区降级为带 GBL 漏洞的旧版 ABL。
";

const FLASH_BDS_EN: &str = "Flash the bootloader bundle from the host:
     fastboot flash abl <vulnerable>.img   (only when the installed ABL lacks the GBL vulnerability)
     fastboot flash efisp BDS.efi";

const FLASH_BDS_ZH: &str = "在主机上刷入引导加载程序套件：
     fastboot flash abl <vulnerable>.img   （仅当已安装的 ABL 不包含 GBL 漏洞时）
     fastboot flash efisp BDS.efi";

const DOWNGRADE_EN: &str = "Downgrade the abl partition to an older ABL with the GBL vulnerability
  (efisp/boot.efi and the abl partition do not need to match versions)";
const DOWNGRADE_ZH: &str = "将 abl 分区降级为带 GBL 漏洞的旧版 ABL
  （efisp/boot.efi 与 abl 分区版本不必一致）";

const MANUAL_EN: &str = "
---- Manual install flow (English) ----
1. Copy the efisp/ folder to the persist boot root:
     cp -r efisp/. /mnt/vendor/persist/efisp/
   (from a custom recovery the boot root is /persist/efisp instead;
    create the directory first if needed, e.g. via MT Manager)
2. sync
";
const MANUAL_ZH: &str = "
---- 手动安装步骤 (中文) ----
1. 将 efisp/ 文件夹复制到 persist 启动根目录：
     cp -r efisp/. /mnt/vendor/persist/efisp/
   （如不存在请先创建 /mnt/vendor/persist/efisp，例如用 MT 管理器）
2. sync
";

pub fn build_report(gbl_patched: bool) -> String {
    let mut report = String::from(OUTPUTS);
    if !gbl_patched {
        report.push_str(GBL_MISSING);
    }
    let english = if gbl_patched {
        format!("3. {FLASH_BDS_EN}")
    } else {
        format!("3. {DOWNGRADE_EN}\n4. {FLASH_BDS_EN}")
    };
    let chinese = if gbl_patched {
        format!("3. {FLASH_BDS_ZH}")
    } else {
        format!("3. {DOWNGRADE_ZH}\n4. {FLASH_BDS_ZH}")
    };
    report.push_str(MANUAL_EN);
    report.push_str(&english);
    report.push_str(MANUAL_ZH);
    report.push_str(&chinese);
    report.push_str("\n========================================");
    report
}
