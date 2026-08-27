#!/system/bin/sh
ui_print "============================================="
ui_print "  Please select language / 请选择语言"
ui_print "  Vol+ = Chinese  |  Vol- = English"
ui_print "============================================="

read_volume_key() {
  case "$(timeout 0.5 getevent -l 2>/dev/null)" in
    *KEY_VOLUMEUP*) echo up ;;
    *KEY_VOLUMEDOWN*) echo down ;;
  esac
}

LANG="zh"
while true; do
  keyevent=$(read_volume_key)
  if [ "$keyevent" = "up" ]; then
    LANG="zh"
    ui_print "[已选择中文 / Chinese selected]"
    break
  elif [ "$keyevent" = "down" ]; then
    LANG="en"
    ui_print "[English selected / 已选择英文]"
    break
  fi
done

echo "$LANG" > "$MODPATH/lang.txt"
ksud module config set user_lang "$LANG" 2>/dev/null


if [ "$LANG" = "zh" ]; then
  echo "name=假回锁" >> "$MODPATH/module.prop"
  echo "description=自动刷新bl相关分区到非活动槽位" >> "$MODPATH/module.prop"
else
  echo "name=Fake BL EFISP" >> "$MODPATH/module.prop"
  echo "description=Automatically flash BL‑related partitions to inactive slot" >> "$MODPATH/module.prop"
fi

if [ "$LANG" = "zh" ]; then
  T_BIN_FAIL="执行失败！"

  T_VERIFY="- 正在验证设备型号"
  T_DEVICE_OK="- 设备验证完成："
  T_PERM="- 正在设置权限"
  T_EFISP_TITLE="确保你的内核没有Baseband Guard，设备BL锁已经解锁"
  T_SOC="确保你的设备是8gen5/8elitegen5"
  T_CHECK_EXP="检测漏洞中..."
  T_INSTALL_CHOICE="请选择是否第一次安装假回锁"
  T_MODE_ASK="请选择启动模式：音量上=Mode 1，音量下=Mode 0"
  T_MODE_2_ASK="请选择启动模式：音量上=保持 Mode 1，音量下=Mode 2"
  T_MODE_RECOVERY_WARN="Mode 1 需要先用 vbmeta 工具 graft 自定义 recovery；未 graft 的 recovery 无法完成数据格式化。是否继续？"
  T_MODE_RECOVERY_NOTE="请使用：vbmetaport <官方 recovery vbmeta> <自定义 recovery.img> <output.img>；输出文件不能变大。"
  T_MODE_RECOVERY_YES="音量上 = 继续"
  T_MODE_RECOVERY_NO="音量下 = 取消"
  T_MODE_VENDOR_ASK="Mode 1 是否修补 vendor_boot？音量上=是，音量下=否"
  T_MODE_VENDOR_YES="已选择修补 vendor_boot"
  T_MODE_VENDOR_NO="已跳过 vendor_boot 修补"
  T_REBOOT_COUNTDOWN="安装完成。需要重启到 recovery 格式化 data，因为假回锁启动链必须在干净的 userdata 上首次初始化。"
  T_VOL_UP="音量上为是（全新安装，需要格式化）"
  T_VOL_DOWN="音量下为否（如果之前安装过一次假回锁或者刚刚首次安装并格式化，建议选否）"
  T_TIP_YES="如果选择是，将会布置 efisp 启动项到 persist 并刷入 BDS 到 efisp，然后重启recovery 进行格式化，格式化后请安装一次这个模块来完成安装，这时选否"
  T_TIP_NO="如果选择否，将跳过本次启动链写入，仅安装模块与 WebUI；每次 OTA 后请在 WebUI 中重新刷写以保留 BL 版本"
  T_SEL_YES="选择了是，正在安装包含补丁的efisp"
  T_NO_SLOT="无法识别当前槽位，已中止安装"
  T_PATCH_FAIL="补丁应用失败，已中止安装"
  T_NO_GBL="检测到当前 ABL 没有 GBL 漏洞"
  T_ABLREPO_CONFIRM="ABL repo 中有带漏洞的旧版 ABL，是否下载并降级 abl 分区？"
  T_ABLREPO_CONFIRM_YES="音量上 = 下载并降级"
  T_ABLREPO_CONFIRM_NO="音量下 = 取消"
  T_ABLREPO_DECLINED="已取消降级，中止安装"
  T_ABLREPO_LOCAL="已从本地模块找到 ABL"
  T_ABLREPO_LOCAL_BAD="本地 ABL 校验失败，尝试云端"
  T_ABLREPO_CLOUD="正在从云端下载 ABL..."
  T_ABLREPO_CLOUD_BAD="云端 ABL 校验失败"
  T_ABLREPO_FAIL="ABL repo 查找失败，请手动刷写带 GBL 漏洞的旧版本 ABL 到 abl 分区后重试"
  T_ABLREPO_DOWNGRADE="正在降级 abl 分区..."
  T_ABLREPO_OK="abl 分区已降级"
  T_ABL_META_FAIL="ABL repo 元数据校验失败"
  T_ABL_CANDIDATE_FAIL="候选 ABL 未通过 GBL 补丁预检，已中止安装"
  T_ABL_DOWNGRADE_GBL_FAIL="降级后的 ABL 仍无 GBL 漏洞，已中止安装"
  T_ABL_NO_DOWNGRADE="当前 ABL 无 GBL 漏洞且未完成降级，已中止安装"
  T_ABL_SIZE_FAIL="ABL 镜像大于目标分区，已中止安装"
  T_ABL_SNAPSHOT_FAIL="ABL 原分区快照失败，已中止安装"
  T_ABL_READBACK_FAIL="ABL 回读校验失败，已中止安装"
  T_ABL_RESTORE_OK="ABL 回读失败，原 ABL 已成功恢复；安装已中止"
  T_ABL_RESTORE_FAIL="ABL 回读失败，原 ABL 恢复失败；安装已中止"
  T_INACTIVE_SLOT_UNRESOLVED="无法解析非活动槽位，跳过配对"
  T_INACTIVE_SLOT_VULN_SKIP="非活动槽位已有 GBL 漏洞，跳过配对"
  T_INACTIVE_SLOT_PAIRING="正在配对非活动槽位 ABL"
  T_INACTIVE_SLOT_PAIR_WARN="警告：非活动槽位 ABL 配对失败，当前安装已保留"
  T_ABL_SETRW_FAIL="abl 分区设置可写失败"
  T_ABL_FLASH_FAIL="abl 分区降级刷写失败"
  T_FLASH_FAIL="efisp 分区刷写失败"
  T_PERSIST_NOT_MOUNTED="persist 分区未挂载到 /mnt/vendor/persist"
  T_EFISP_DIR_FAIL="创建 efisp 启动目录失败"
  T_EFISP_WRITE_FAIL="写入 efisp 启动文件失败"
  T_PLACE_BOOT="正在布置 efisp 启动项到 persist"
  T_FLASH_BDS="正在刷入 BDS 到 efisp"
  T_DONE_YES="安装完成，请重启到recovery进行格式化，格式化后请安装一次这个模块来完成安装，这时选否"
  T_SEL_NO="选择了否，跳过启动链写入并保留模块 WebUI"
  T_DONE_NO="模块安装完成，请重启系统；OTA 后请使用 WebUI 刷写"
  T_SUPPLIED_ABL="检测到 /data/local/tmp/canoe/abl.img,是否用它替代分区读取?音量上=是,音量下=否(读取分区)"
  T_SUPPLIED_VBMETA="检测到 /data/local/tmp/canoe/vbmeta.img,是否用它替代分区读取?音量上=是,音量下=否(读取分区)"
  T_FORMAT_WHY="首次安装必须格式化 data，不是可选步骤。Mode 1 会向 OS 投影锁定的 DeviceInfo，TEE 会拒绝在之前状态下写入 userdata 的 data key，所以旧 data 无论如何都不可读。
设备上：主菜单 -> Reboot to Recovery -> FORMAT DATA。
canoe.cfg 带有 devinfo-repair asneeded，下一次受管启动时会修复锁定状态；格式化才能让该状态一致。"
  T_SIGNER_CHANGED="vbmeta 签名者已变化。这在切换到或离开自定义 ROM 时是预期的；此处没有任何工具能证明哪个密钥才是 OEM 的。Mode 2 profile 不会被安装。"
  T_BIN_FAIL="执行失败！"
else

  T_VERIFY="- Verifying device model"
  T_DEVICE_OK="- Device verified:"
  T_PERM="- Setting permissions"
  T_EFISP_TITLE="Ensure kernel has no Baseband Guard and BL bootloader is unlocked"
  T_SOC="Ensure device is 8gen5 / 8elitegen5"
  T_CHECK_EXP="Detecting exploit..."
  T_INSTALL_CHOICE="Is this your first time installing Fake BL EFISP?"
  T_MODE_ASK="Select boot mode: Vol+ = Mode 1, Vol- = Mode 0"
  T_MODE_2_ASK="Select boot mode: Vol+ = keep Mode 1, Vol- = Mode 2"
  T_MODE_RECOVERY_WARN="Mode 1 requires grafting a custom recovery with the vbmeta tool first; without that graft data formatting cannot complete. Continue?"
  T_MODE_RECOVERY_NOTE="Graft with: vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>; the output must not grow."
  T_MODE_RECOVERY_YES="Vol+ = continue"
  T_MODE_RECOVERY_NO="Vol- = cancel"
  T_MODE_VENDOR_ASK="Mode 1: patch vendor_boot? Vol+ = yes, Vol- = no"
  T_MODE_VENDOR_YES="vendor_boot patch selected"
  T_MODE_VENDOR_NO="vendor_boot patch skipped"
  T_REBOOT_COUNTDOWN="Install complete. Reboot to recovery and format data because the first fake-lock boot-chain initialization needs clean userdata."
  T_VOL_UP="Vol+ = YES (Fresh install, requires format)"
  T_VOL_DOWN="Vol‑ = NO (If installed before or just formatted)"
  T_TIP_YES="If YES: efisp boot entries placed on persist and BDS flashed to efisp, reboot to recovery and format data, then reinstall this module and select NO"
  T_TIP_NO="If NO: skip boot-chain writes and install only the module/WebUI; after each OTA, use the WebUI to retain the BL version"
  T_SEL_YES="Selected YES, installing patched efisp"
  T_NO_SLOT="Failed to detect current slot, abort"
  T_PATCH_FAIL="Failed to apply patch, abort"
  T_NO_GBL="Current ABL lacks the GBL vulnerability"
  T_ABLREPO_CONFIRM="An older ABL with the GBL vuln is available in the ABL repo. Download and downgrade the abl partition?"
  T_ABLREPO_CONFIRM_YES="Vol+ = download and downgrade"
  T_ABLREPO_CONFIRM_NO="Vol‑ = cancel"
  T_ABLREPO_DECLINED="Downgrade cancelled, aborting"
  T_ABLREPO_LOCAL="Found ABL in local module"
  T_ABLREPO_LOCAL_BAD="Local ABL verification failed, trying cloud"
  T_ABLREPO_CLOUD="Downloading ABL from cloud..."
  T_ABLREPO_CLOUD_BAD="Cloud ABL verification failed"
  T_ABLREPO_FAIL="ABL repo lookup failed. Manually flash an older ABL with the GBL vulnerability to the abl partition, then retry"
  T_ABLREPO_DOWNGRADE="Downgrading the abl partition..."
  T_ABLREPO_OK="abl partition downgraded"
  T_ABL_SETRW_FAIL="Failed to set abl to read‑write"
  T_ABL_FLASH_FAIL="Failed to flash abl partition"
  T_ABL_META_FAIL="ABL repo metadata verification failed"
  T_ABL_CANDIDATE_FAIL="Candidate ABL failed the GBL patch preflight; aborting"
  T_ABL_DOWNGRADE_GBL_FAIL="Downgraded ABL still lacks the GBL vulnerability; aborting"
  T_ABL_NO_DOWNGRADE="Current ABL lacks GBL and no downgrade completed; aborting"
  T_ABL_SIZE_FAIL="ABL image is larger than the target partition; aborting"
  T_ABL_SNAPSHOT_FAIL="Failed to snapshot the outgoing ABL partition; aborting"
  T_ABL_READBACK_FAIL="ABL readback verification failed; aborting"
  T_ABL_RESTORE_OK="ABL readback failed; the original ABL was restored; aborting"
  T_ABL_RESTORE_FAIL="ABL readback failed; restoring the original ABL failed; aborting"
  T_INACTIVE_SLOT_UNRESOLVED="Cannot resolve the inactive slot; skipping ABL pairing"
  T_INACTIVE_SLOT_VULN_SKIP="Inactive-slot ABL already has the GBL vulnerability; skipping pairing"
  T_INACTIVE_SLOT_PAIRING="Pairing the inactive-slot ABL"
  T_INACTIVE_SLOT_PAIR_WARN="Warning: inactive-slot ABL pairing failed; committed install retained"
  T_FLASH_FAIL="Failed to flash efisp"
  T_PERSIST_NOT_MOUNTED="persist is not mounted at /mnt/vendor/persist"
  T_EFISP_DIR_FAIL="efisp boot dir create failed"
  T_EFISP_WRITE_FAIL="efisp boot file write failed"
  T_PLACE_BOOT="Placing efisp boot entries on persist"
  T_FLASH_BDS="Flashing BDS to efisp"
  T_DONE_YES="Install complete. Reboot to recovery and format data, then reinstall module and choose NO"
  T_SEL_NO="Selected NO, skipping boot-chain writes and keeping the module WebUI"
  T_DONE_NO="Module install complete; reboot, then use the WebUI after each OTA"
  T_SUPPLIED_ABL="Found /data/local/tmp/canoe/abl.img — use it instead of reading the partition? Vol+ = yes, Vol- = no (read the partition)"
  T_SUPPLIED_VBMETA="Found /data/local/tmp/canoe/vbmeta.img — use it instead of reading the partition? Vol+ = yes, Vol- = no (read the partition)"
  T_FORMAT_WHY="Data format is required. On a first-time installation it is not optional: Mode 1 projects a locked DeviceInfo to the OS, and the TEE will refuse the data key for userdata written under the previous state, so the old data is unreadable either way.
On the device: main menu -> Reboot to Recovery -> FORMAT DATA.
canoe.cfg carries devinfo-repair asneeded, so the lock-state repair happens on the next managed launch; formatting is what makes that state coherent."
  T_SIGNER_CHANGED="The vbmeta signer changed. This is expected when moving to or from a custom ROM; no tool here can prove which key is the OEM's. The Mode 2 profile will not be installed."
  T_BIN_FAIL="Binary execution failed!"
fi

ui_print "$T_VERIFY"
_model=$(getprop ro.product.model 2>/dev/null)
_name=$(getprop ro.product.name 2>/dev/null)
_inc=$(getprop ro.build.version.incremental 2>/dev/null)
ui_print "$T_DEVICE_OK $_model / $_name / $_inc"
ui_print "$T_PERM"

set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644
set_perm "$MODPATH/webroot/api" 0 0 0755
set_perm "$MODPATH/module.prop" 0 0 0644
set_perm "$MODPATH/customize.sh" 0 0 0755

# ========== 修改1：槽位检测函数前移，提前定义 ==========
detect_current_slot() {
  case "$(getprop ro.boot.slot_suffix 2>/dev/null)" in
    _a) echo _a ;;
    _b) echo _b ;;
    *) return 1 ;;
  esac
}

EXTRA_PATCH_MODE=""

# ========== 修改2：执行修补前获取当前槽位，提取a/b字母 ==========
current_slot_suffix=$(detect_current_slot)
if [ -z "$current_slot_suffix" ]; then
  ui_print "$T_NO_SLOT"
  abort "slot detection failed"
fi
slot_letter=${current_slot_suffix#_}  # 去掉下划线前缀，得到纯字母 a 或 b

# Defer optional partition mutation until ABL/vbmeta/profile preflight and any
# required GBL-vulnerable ABL downgrade have completed successfully.

BY_NAME_DIR=${BY_NAME_DIR:-/dev/block/by-name}
RUNTIME_DIR=${RUNTIME_DIR:-$MODPATH/tmp}
PERSIST_MNT=${PERSIST_MNT:-/mnt/vendor/persist}
EFISP_DIR=${EFISP_DIR:-$PERSIST_MNT/efisp}
ABLREPO_URL=${ABLREPO_URL:-"https://raw.githubusercontent.com/1vivy/gbl_root_canoe/main/ablrepo"}
mkdir -p "$RUNTIME_DIR"

# Verify $1 against the sha256 in $2 (first whitespace‑delimited token).
verify_sha256() {
  [ -f "$1" ] && [ -f "$2" ] || return 1
  expected=$(cut -d' ' -f1 "$2" | tr -d '[:space:]')
  actual=$(sha256sum "$1" | cut -d' ' -f1 | tr -d '[:space:]')
  [ -n "$expected" ] && [ "$expected" = "$actual" ]
}

# Download $1 into $2 using whichever fetcher is available.
download_url() {
  if command -v wget >/dev/null 2>&1; then
    timeout 60 wget -O "$2" "$1" >/dev/null 2>&1
  elif command -v curl >/dev/null 2>&1; then
    timeout 60 curl -fL -o "$2" "$1" >/dev/null 2>&1
  else
    return 1
  fi
}

# Read a required key from the ABL repository metadata file.
abl_meta_value() {
  sed -n "s/^$1=//p" "$2" | tail -n 1
}

verify_abl_metadata() {
  metadata="$1"
  image="$2"
  product=$(getprop ro.product.name 2>/dev/null)
  model=$(getprop ro.product.model 2>/dev/null)
  soc=$(getprop ro.board.platform 2>/dev/null)
  [ -f "$metadata" ] && [ -f "$image" ] || return 1
  meta_product=$(abl_meta_value product "$metadata")
  meta_model=$(abl_meta_value model "$metadata")
  meta_soc=$(abl_meta_value soc "$metadata")
  meta_abl_version=$(abl_meta_value abl_version "$metadata")
  meta_sha256=$(abl_meta_value sha256 "$metadata")
  meta_bytes=$(abl_meta_value bytes "$metadata")
  [ "$meta_product" = "$product" ] || return 1
  [ -n "$meta_model" ] && [ -n "$meta_soc" ] &&
    [ -n "$meta_abl_version" ] || return 1
  [ -n "$meta_sha256" ] && [ -n "$meta_bytes" ] || return 1
  image_sha256=$(sha256sum "$image" | cut -d' ' -f1 | tr -d '[:space:]')
  image_bytes=$(wc -c < "$image" | tr -d '[:space:]')
  [ "$meta_sha256" = "$image_sha256" ] || return 1
  [ "$meta_bytes" = "$image_bytes" ] || return 1
  if [ "$meta_model" != "unknown" ] && [ "$meta_model" != "$model" ]; then
    return 1
  fi
  if [ "$meta_soc" != "unknown" ] && [ "$meta_soc" != "$soc" ]; then
    return 1
  fi
  return 0
}

# Download $1 into $2 using whichever fetcher is available.
download_url() {
  if command -v wget >/dev/null 2>&1; then
    timeout 60 wget -O "$2" "$1" >/dev/null 2>&1
  elif command -v curl >/dev/null 2>&1; then
    timeout 60 curl -fL -o "$2" "$1" >/dev/null 2>&1
  else
    return 1
  fi
}

# Fetch an older ABL with the GBL vulnerability. Looks up the local module
# bundle first, then the cloud. On success, leaves the image at
# $RUNTIME_DIR/repo_abl.img and returns 0; otherwise returns 1.
fetch_abl_from_repo() {
  product=$(getprop ro.product.name 2>/dev/null)
  [ -z "$product" ] && return 1
  local_dir="$MODPATH/ablrepo/$product"
  if [ -f "$local_dir/abl.img" ]; then
    if [ ! -f "$local_dir/abl.sha256" ] ||
       ! verify_sha256 "$local_dir/abl.img" "$local_dir/abl.sha256"; then
      ui_print "$T_ABLREPO_LOCAL_BAD"
    elif ! verify_abl_metadata "$local_dir/abl.meta" "$local_dir/abl.img"; then
      ui_print "$T_ABL_META_FAIL"
    elif ! cp "$local_dir/abl.img" "$RUNTIME_DIR/repo_abl.img"; then
      ui_print "$T_ABLREPO_LOCAL_BAD"
    else
      ui_print "$T_ABLREPO_LOCAL"
      return 0
    fi
  fi
  ui_print "$T_ABLREPO_CLOUD"
  if download_url "$ABLREPO_URL/$product/abl.meta" "$RUNTIME_DIR/repo_abl.meta" &&
     download_url "$ABLREPO_URL/$product/abl.sha256" "$RUNTIME_DIR/repo_abl.sha256" &&
     download_url "$ABLREPO_URL/$product/abl.img" "$RUNTIME_DIR/repo_abl.img"; then
    if ! verify_sha256 "$RUNTIME_DIR/repo_abl.img" "$RUNTIME_DIR/repo_abl.sha256"; then
      ui_print "$T_ABLREPO_CLOUD_BAD"
    elif ! verify_abl_metadata "$RUNTIME_DIR/repo_abl.meta" \
         "$RUNTIME_DIR/repo_abl.img"; then
      ui_print "$T_ABL_META_FAIL"
    else
      return 0
    fi
  fi
  return 1
}
MODE2_PROFILE="$MODPATH/bin/mode2_profile"
ABL_TZMAP="$MODPATH/bin/abl_tzmap"
abl_part="$BY_NAME_DIR/abl$current_slot_suffix"
vbmeta_part="$BY_NAME_DIR/vbmeta$current_slot_suffix"
abl_source="$abl_part"
vbmeta_source="$vbmeta_part"
signer_source=partition
export CANOE_ALLOW_NEW_SIGNER=1
SUPPLIED_DIR=${SUPPLIED_DIR:-/data/local/tmp/canoe}

select_image_sources() {
  if [ -s "$SUPPLIED_DIR/abl.img" ]; then
    ui_print "$T_SUPPLIED_ABL"
    keyevent=$(read_volume_key)
    if [ "$keyevent" = "up" ]; then
      abl_source="$SUPPLIED_DIR/abl.img"
    fi
  fi
  if [ -s "$SUPPLIED_DIR/vbmeta.img" ]; then
    ui_print "$T_SUPPLIED_VBMETA"
    keyevent=$(read_volume_key)
    if [ "$keyevent" = "up" ]; then
      vbmeta_source="$SUPPLIED_DIR/vbmeta.img"
      signer_source=supplied
    fi
  fi
}

preflight_candidate_abl() {
  candidate="$1"
  candidate_dir="$RUNTIME_DIR/candidate"
  rm -rf "$candidate_dir"
  mkdir -p "$candidate_dir" || return 1
  if ! "$MODPATH/bin/extractfv" -o "$candidate_dir" -v "$candidate" \
       > "$RUNTIME_DIR/candidate.extract.log" 2>&1 ||
     ! "$MODPATH/bin/patch_abl" "$candidate_dir/LinuxLoader.efi" \
       "$candidate_dir/patched.efi" > "$RUNTIME_DIR/candidate.patch.log" 2>&1 ||
     [ ! -s "$candidate_dir/patched.efi" ]; then
    return 1
  fi
  if grep -q "Warning: Failed to patch ABL GBL" \
       "$RUNTIME_DIR/candidate.patch.log"; then
    return 1
  fi
  return 0
}

preflight_current_pair() {
  rm -f "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" \
    "$RUNTIME_DIR/patch.log" "$RUNTIME_DIR/boot.efi.gm2p" \
    "$RUNTIME_DIR/boot.efi.tzmap"
  CURRENT_PAIR_GBL_VULNERABLE=1
  if ! "$MODPATH/bin/extractfv" -o "$RUNTIME_DIR" -v "$abl_source" > "$RUNTIME_DIR/extract.log" 2>&1 ||
     ! "$MODPATH/bin/patch_abl" "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" > "$RUNTIME_DIR/patch.log" 2>&1 ||
     [ ! -s "$RUNTIME_DIR/patched.efi" ]; then
    ui_print "$T_PATCH_FAIL"
    return 1
  fi
  if grep -q "Warning: Failed to patch ABL GBL" "$RUNTIME_DIR/patch.log"; then
    CURRENT_PAIR_GBL_VULNERABLE=0
  fi
  if [ ! -x "$MODE2_PROFILE" ] ||
     ! "$MODE2_PROFILE" derive --vbmeta "$vbmeta_source" --out "$RUNTIME_DIR/boot.efi.gm2p" > "$RUNTIME_DIR/profile.log" 2>&1 ||
     [ ! -s "$RUNTIME_DIR/boot.efi.gm2p" ] ||
     ! "$MODE2_PROFILE" validate --input "$RUNTIME_DIR/boot.efi.gm2p" >> "$RUNTIME_DIR/profile.log" 2>&1; then
    ui_print "$T_PATCH_FAIL"
    rm -f "$RUNTIME_DIR/boot.efi.gm2p"
    return 1
  fi
  # --allow-incomplete: an ABL with no recorded RE evidence still gets a sidecar
  # carrying the soundly derived identifier flags.
  if [ ! -x "$ABL_TZMAP" ] ||
     ! "$ABL_TZMAP" derive "$RUNTIME_DIR/LinuxLoader.efi" \
       -o "$RUNTIME_DIR/boot.efi.tzmap" --allow-incomplete \
       > "$RUNTIME_DIR/tzmap.log" 2>&1 ||
     [ ! -s "$RUNTIME_DIR/boot.efi.tzmap" ] ||
     ! "$ABL_TZMAP" validate "$RUNTIME_DIR/boot.efi.tzmap" >> "$RUNTIME_DIR/tzmap.log" 2>&1; then
    ui_print "$T_PATCH_FAIL"
    rm -f "$RUNTIME_DIR/boot.efi.tzmap"
    return 1
  fi
  return 0
}

abl_source_bytes() {
  if [ -b "$1" ]; then
    blockdev --getsize64 "$1" 2>/dev/null
    return
  fi
  wc -c < "$1" 2>/dev/null | tr -d '[:space:]'
}

abl_hash() {
  sha256sum "$1" 2>/dev/null | cut -d' ' -f1 | tr -d '[:space:]'
}

# Hash the first $2 bytes of $1 without materialising a copy: a byte-at-a-time
# readback of a firmware partition costs minutes on device.
abl_region_hash() {
  region_dev="$1"
  region_bytes="$2"
  region_blocks=$(( (region_bytes + 4095) / 4096 ))
  dd if="$region_dev" bs=4096 count="$region_blocks" 2>/dev/null |
    head -c "$region_bytes" | sha256sum | cut -d' ' -f1 | tr -d '[:space:]'
}

restore_abl_snapshot() {
  restore_target="$1"
  restore_snapshot="$2"
  restore_bytes="$3"
  if ! dd if="$restore_snapshot" of="$restore_target" bs=4M conv=fsync \
       >> "$RUNTIME_DIR/flash.log" 2>&1 ||
     ! sync; then
    return 1
  fi
  [ "$(abl_hash "$restore_snapshot")" = \
    "$(abl_region_hash "$restore_target" "$restore_bytes")" ]
}

flash_abl_image() {
  flash_source="$1"
  flash_target="$2"
  flash_snapshot="$3"
  ABL_FLASH_STATUS=""
  flash_target_bytes=$(blockdev --getsize64 "$flash_target" 2>/dev/null)
  flash_source_bytes=$(abl_source_bytes "$flash_source")
  case "$flash_target_bytes" in ''|*[!0-9]*) ABL_FLASH_STATUS=size; return 1 ;; esac
  case "$flash_source_bytes" in ''|*[!0-9]*) ABL_FLASH_STATUS=size; return 1 ;; esac
  if [ "$flash_source_bytes" -le 0 ] ||
     [ "$flash_source_bytes" -gt "$flash_target_bytes" ]; then
    ABL_FLASH_STATUS=size
    return 1
  fi
  if ! blockdev --setrw "$flash_target" >> "$RUNTIME_DIR/flash.log" 2>&1; then
    ABL_FLASH_STATUS=setrw
    return 1
  fi
  rm -f "$flash_snapshot"
  if ! dd if="$flash_target" of="$flash_snapshot" bs=4M conv=fsync \
       >> "$RUNTIME_DIR/flash.log" 2>&1 ||
     ! sync; then
    ABL_FLASH_STATUS=snapshot
    return 1
  fi
  if ! dd if="$flash_source" of="$flash_target" bs=4M conv=fsync \
       >> "$RUNTIME_DIR/flash.log" 2>&1; then
    ABL_FLASH_STATUS=flash
    return 1
  fi
  if ! sync; then
    ABL_FLASH_STATUS=sync
    return 1
  fi
  if [ "$(abl_hash "$flash_source")" != \
       "$(abl_region_hash "$flash_target" "$flash_source_bytes")" ]; then
    if restore_abl_snapshot "$flash_target" "$flash_snapshot" \
         "$flash_target_bytes"; then
      ABL_FLASH_STATUS=readback_restore_ok
    else
      ABL_FLASH_STATUS=readback_restore_fail
    fi
    return 1
  fi
  return 0
}

abl_is_gbl_vulnerable() {
  inspect_abl="$1"
  inspect_dir="$2"
  rm -rf "$inspect_dir"
  mkdir -p "$inspect_dir" || return 1
  if ! "$MODPATH/bin/extractfv" -o "$inspect_dir" -v "$inspect_abl" \
       > "$inspect_dir/extract.log" 2>&1 ||
     ! "$MODPATH/bin/patch_abl" "$inspect_dir/LinuxLoader.efi" \
       "$inspect_dir/patched.efi" > "$inspect_dir/patch.log" 2>&1; then
    return 1
  fi
  if ! grep -q "Warning: Failed to patch ABL GBL" "$inspect_dir/patch.log"; then
    return 0
  fi
  return 1
}

pair_inactive_abl() {
  inactive_slot_suffix=
  case "$current_slot_suffix" in
    _a) inactive_slot_suffix=_b ;;
    _b) inactive_slot_suffix=_a ;;
    *) ui_print "$T_INACTIVE_SLOT_UNRESOLVED"; return 0 ;;
  esac
  inactive_abl_part="$BY_NAME_DIR/abl$inactive_slot_suffix"
  if abl_is_gbl_vulnerable "$inactive_abl_part" "$RUNTIME_DIR/inactive-abl"; then
    ui_print "$T_INACTIVE_SLOT_VULN_SKIP"
    return 0
  fi
  ui_print "$T_INACTIVE_SLOT_PAIRING"
  if flash_abl_image "$abl_part" "$inactive_abl_part" \
       "$RUNTIME_DIR/inactive_abl_pre.img"; then
    return 0
  fi
  ui_print "$T_INACTIVE_SLOT_PAIR_WARN"
  return 0
}



install_pair() {
  target="$1"
  stage="$target/.canoe.stage.$$"
  rm -rf "$stage"
  mkdir -p "$stage" || return 1
  if ! cp "$RUNTIME_DIR/patched.efi" "$stage/boot.efi" ||
     ! cp "$RUNTIME_DIR/boot.efi.gm2p" "$stage/boot.efi.gm2p" ||
     ! cp "$RUNTIME_DIR/boot.efi.tzmap" "$stage/boot.efi.tzmap" ||
     ! cp "$MODPATH/BDS.efi" "$stage/BDS.efi"; then
    rm -rf "$stage"
    return 1
  fi
  if [ -d "$MODPATH/efisp/tools" ] &&
     { ! mkdir -p "$stage/tools" ||
       ! cp -r "$MODPATH/efisp/tools/." "$stage/tools/"; }; then
    rm -rf "$stage"
    return 1
  fi
  if ! "$ABL_TZMAP" verify --sidecar "$stage/boot.efi.tzmap" \
       --abl "$RUNTIME_DIR/LinuxLoader.efi" --allow-zero-digest \
       >> "$RUNTIME_DIR/tzmap.log" 2>&1; then
    rm -rf "$stage"
    return 1
  fi
  transaction_log="$RUNTIME_DIR/transaction.log"
  rm -f "$transaction_log"
  if ! CANOE_ALLOW_NEW_SIGNER=1 CANOE_SIGNER_SOURCE="$signer_source" \
       CANOE_MODE="$selected_mode" CANOE_ACTIVE_SLOT="$current_slot_suffix" \
       CANOE_BOOT_ENTRY="$MODPATH/canoe_boot_entry.sh" \
       sh "$MODPATH/canoe_device_install.sh" "$stage" "$target" \
       "$BY_NAME_DIR/efisp" "$RUNTIME_DIR/efisp.backup" \
       > "$transaction_log" 2>&1; then
    cat "$transaction_log" >> "$RUNTIME_DIR/flash.log"
    rm -rf "$stage"
    return 1
  fi
  cat "$transaction_log" >> "$RUNTIME_DIR/flash.log"
  if grep -q 'CANOE-MARK: signer-changed' "$transaction_log"; then
    ui_print "$T_SIGNER_CHANGED"
    if [ "$signer_source" != "supplied" ] && [ "$selected_mode" = "2" ]; then
      entry_id=android-${current_slot_suffix#_}
      if ! sh "$MODPATH/canoe_boot_entry.sh" mode "$target" \
           --id "$entry_id" --mode 1 >> "$RUNTIME_DIR/flash.log" 2>&1; then
        rm -rf "$stage"
        return 1
      fi
      printf '%s\n' 'Mode 2 downgraded to Mode 1 after signer change' \
        >> "$RUNTIME_DIR/flash.log"
    fi
  fi
  rm -rf "$stage"
  return 0
}




run_optional_patch() {
  if [ "$EXTRA_PATCH_MODE" = "vendor_boot" ]; then
    ui_print "$T_MODE_VENDOR_YES"
    ui_print "- 当前槽位: $slot_letter"
    if [ ! -x "$MODPATH/bin/canoe_vendor_boot.sh" ]; then
      ui_print "$T_BIN_FAIL: canoe_vendor_boot.sh binary not found!"
      abort "canoe_vendor_boot.sh missing"
    fi
    if ! sh "$MODPATH/bin/canoe_vendor_boot.sh" "$slot_letter"; then
      ui_print "$T_BIN_FAIL (vendor_boot)"
      abort "vendor_boot patch failed"
    fi
    ui_print "$T_MODE_VENDOR_YES"
  fi
}

ui_print "$T_EFISP_TITLE"
ui_print "$T_SOC"
ui_print "$T_CHECK_EXP"
current_slot=$(detect_current_slot)

ui_print "$T_INSTALL_CHOICE"
ui_print "$T_VOL_UP"
ui_print "$T_VOL_DOWN"
ui_print "$T_TIP_YES"
ui_print "$T_TIP_NO"

while true; do
  keyevent=$(read_volume_key)
  if [ "$keyevent" = "up" ]; then
    ui_print "$T_SEL_YES"
    if [ -z "$current_slot" ]; then
      ui_print "$T_NO_SLOT"
      abort "slot detection failed"
    fi

    selected_mode=${CANOE_MODE:-}
    case "$selected_mode" in 0|1|2) ;; *) selected_mode= ;;
    esac
    if [ -z "$selected_mode" ]; then
      ui_print "$T_MODE_ASK"
      keyevent=$(read_volume_key)
      if [ "$keyevent" = "down" ]; then
        selected_mode=0
      else
        ui_print "$T_MODE_2_ASK"
        keyevent=$(read_volume_key)
        if [ "$keyevent" = "down" ]; then
          selected_mode=2
        else
          selected_mode=1
        fi
      fi
    fi
    select_image_sources

    if ! preflight_current_pair; then
      abort "ABL/vbmeta/profile preflight failed"
    fi
    initial_pair_gbl_vulnerable="$CURRENT_PAIR_GBL_VULNERABLE"
    abl_downgrade_done=0
    if [ "$initial_pair_gbl_vulnerable" = "0" ]; then
      ui_print "$T_NO_GBL"
      ui_print "$T_ABLREPO_CONFIRM"
      ui_print "$T_ABLREPO_CONFIRM_YES"
      ui_print "$T_ABLREPO_CONFIRM_NO"
      repo_confirm=""
      while [ -z "$repo_confirm" ]; do
        keyevent=$(read_volume_key)
        if [ "$keyevent" = "up" ]; then
          repo_confirm=yes
        elif [ "$keyevent" = "down" ]; then
          repo_confirm=no
        fi
      done
      if [ "$repo_confirm" = "no" ]; then
        ui_print "$T_ABLREPO_DECLINED"
        abort "downgrade declined"
      fi
      if ! fetch_abl_from_repo; then
        ui_print "$T_ABLREPO_FAIL"
        abort "abl repo lookup failed"
      fi
      if ! preflight_candidate_abl "$RUNTIME_DIR/repo_abl.img"; then
        ui_print "$T_ABL_CANDIDATE_FAIL"
        abort "candidate ABL preflight failed"
      fi
      ui_print "$T_ABLREPO_DOWNGRADE"
      if ! flash_abl_image "$RUNTIME_DIR/repo_abl.img" "$abl_part" \
           "$RUNTIME_DIR/abl_pre.img"; then
        case "$ABL_FLASH_STATUS" in
          setrw) ui_print "$T_ABL_SETRW_FAIL"; abort "setrw abl failed" ;;
          size) ui_print "$T_ABL_SIZE_FAIL"; abort "abl image does not fit" ;;
          snapshot) ui_print "$T_ABL_SNAPSHOT_FAIL"; abort "abl snapshot failed" ;;
          readback_restore_ok) ui_print "$T_ABL_RESTORE_OK"; abort "abl readback mismatch; restore succeeded" ;;
          readback_restore_fail) ui_print "$T_ABL_RESTORE_FAIL"; abort "abl readback mismatch; restore failed" ;;
          sync) ui_print "$T_ABL_FLASH_FAIL"; abort "sync downgraded abl failed" ;;
          *) ui_print "$T_ABL_FLASH_FAIL"; abort "downgrade abl failed" ;;
        esac
      fi
      if ! preflight_current_pair; then
        abort "ABL/vbmeta/profile preflight after downgrade failed"
      fi
      if [ "$CURRENT_PAIR_GBL_VULNERABLE" != "1" ]; then
        ui_print "$T_ABL_DOWNGRADE_GBL_FAIL"
        abort "downgraded ABL is not GBL-vulnerable"
      fi
      abl_downgrade_done=1
      ui_print "$T_ABLREPO_OK"
    fi
    if [ "$initial_pair_gbl_vulnerable" = "0" ] &&
       [ "$abl_downgrade_done" != "1" ]; then
      ui_print "$T_ABL_NO_DOWNGRADE"
      abort "non-vulnerable ABL was not downgraded"
    fi

    EXTRA_PATCH_MODE=skip
    if [ "$selected_mode" = "1" ]; then
      ui_print "$T_MODE_RECOVERY_WARN"
      ui_print "$T_MODE_RECOVERY_NOTE"
      ui_print "$T_MODE_RECOVERY_YES"
      ui_print "$T_MODE_RECOVERY_NO"
      keyevent=$(read_volume_key)
      [ "$keyevent" = "up" ] || abort "custom recovery graft declined"
      ui_print "$T_MODE_VENDOR_ASK"
      keyevent=$(read_volume_key)
      if [ "$keyevent" = "up" ]; then
        EXTRA_PATCH_MODE=vendor_boot
        ui_print "$T_MODE_VENDOR_YES"
      else
        ui_print "$T_MODE_VENDOR_NO"
      fi
    fi

    ui_print "$T_PLACE_BOOT"
    if ! grep -q " $PERSIST_MNT " /proc/mounts; then
      ui_print "$T_PERSIST_NOT_MOUNTED"
      abort "persist not mounted"
    fi
    mkdir -p "$EFISP_DIR" || { ui_print "$T_EFISP_DIR_FAIL"; abort "efisp mkdir failed"; }
    run_optional_patch
    ui_print "$T_FLASH_BDS"
    if ! install_pair "$EFISP_DIR"; then
      ui_print "$T_EFISP_WRITE_FAIL"
      ui_print "$T_FLASH_FAIL"
      abort "efisp pair write failed"
    fi

    pair_inactive_abl
    ui_print "$T_DONE_YES"
    printf '%s\n' "$T_FORMAT_WHY" |
      while IFS= read -r format_line; do ui_print "$format_line"; done
    ui_print "$T_REBOOT_COUNTDOWN"
    countdown=5
    while [ "$countdown" -gt 0 ]; do
      ui_print "$countdown"
      sleep 1
      countdown=$((countdown - 1))
    done
    command -v reboot >/dev/null 2>&1 && reboot recovery
    rm -rf "$RUNTIME_DIR"
    break
  elif [ "$keyevent" = "down" ]; then
    ui_print "$T_SEL_NO"
    ui_print "$T_DONE_NO"
    rm -rf "$RUNTIME_DIR"
    break
  fi
done

# ablrepo is bundled only for install‑time ABL downgrade lookup. Remove it so
# the device‑side module dir (/data/adb/modules/fake_bl_efisp) stays lean after
# installation; the cloud URL remains available for later re‑downloads.
rm -rf "$MODPATH/ablrepo"