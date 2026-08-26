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
  T_OPT_MENU="====================================="
  T_OPT_ASK="是否启用额外修补功能(vendor_boot/super)?"
  T_OPT_UP_YES="音量上 = 启用修补"
  T_OPT_DOWN_SKIP="音量下 = 跳过修补"
  T_OPT_CHOICE1="请选择修补类型"
  T_OPT_VB="音量上：仅修补 vendor_boot"
  T_OPT_SUPER="音量下：移除super分区验证"
  T_OPT_RUN_VB="- 开始执行vendor_boot修补..."
  T_OPT_RUN_SUPER="- 开始执行移除super验证..."
  T_OPT_FINISH_VB="vendor_boot修补执行完成"
  T_OPT_FINISH_SUPER="super验证移除执行完成！"
  T_OPT_SUPER_NOTE="【重要提示】移除super验证已内置vendor_boot修补；操作后请勿修改 system、system_dlkm、vendor 分区！"
  T_OPT_SKIP="已跳过额外修补步骤"
  T_BIN_FAIL="执行失败！"

  T_VERIFY="- 正在验证设备型号"
  T_DEVICE_OK="- 设备验证完成："
  T_PERM="- 正在设置权限"
  T_EFISP_TITLE="确保你的内核没有Baseband Guard，设备BL锁已经解锁"
  T_SOC="确保你的设备是8gen5/8elitegen5"
  T_CHECK_EXP="检测漏洞中..."
  T_INSTALL_CHOICE="请选择是否第一次安装假回锁"
  T_MODE_ASK="请选择启动模式：音量上=Mode 1，音量下=Mode 0（Mode 2 可在 WebUI 为当前启动项设置）"
  T_MODE_RECOVERY_WARN="Mode 1 需要先用 vbmeta 工具 graft 自定义 recovery；未 graft 的 recovery 无法完成数据格式化。是否继续？"
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
  T_TZMAP_BIND_FAIL="TrustZone map 与闪存中的 ABL 不匹配，已回滚安装"
  T_INACTIVE_SLOT_UNRESOLVED="无法解析非活动槽位，跳过配对"
  T_INACTIVE_SLOT_VULN_SKIP="非活动槽位已有 GBL 漏洞，跳过配对"
  T_INACTIVE_SLOT_PAIRING="正在配对非活动槽位 ABL"
  T_INACTIVE_SLOT_PAIR_WARN="警告：非活动槽位 ABL 配对失败，当前安装已保留"
  T_ABL_SETRW_FAIL="abl 分区设置可写失败"
  T_ABL_FLASH_FAIL="abl 分区降级刷写失败"
  T_SETRW_FAIL="efisp 分区设置可写失败"
  T_FLASH_FAIL="efisp 分区刷写失败"
  T_PERSIST_NOT_MOUNTED="persist 分区未挂载到 /mnt/vendor/persist"
  T_EFISP_DIR_FAIL="创建 efisp 启动目录失败"
  T_EFISP_WRITE_FAIL="写入 efisp 启动文件失败"
  T_PLACE_BOOT="正在布置 efisp 启动项到 persist"
  T_FLASH_BDS="正在刷入 BDS 到 efisp"
  T_DONE_YES="安装完成，请重启到recovery进行格式化，格式化后请安装一次这个模块来完成安装，这时选否"
  T_SEL_NO="选择了否，跳过启动链写入并保留模块 WebUI"
  T_DONE_NO="模块安装完成，请重启系统；OTA 后请使用 WebUI 刷写"
else
  T_OPT_MENU="====================================="
  T_OPT_ASK="Enable extra patch functions?"
  T_OPT_UP_YES="Vol+ = Enable patches"
  T_OPT_DOWN_SKIP="Vol‑ = Skip patches"
  T_OPT_CHOICE1="Select patch mode"
  T_OPT_VB="Vol+ : Patch vendor_boot only"
  T_OPT_SUPER="Vol‑ : Remove super partition verification"
  T_OPT_RUN_VB="- Running vendor_boot patch binary..."
  T_OPT_RUN_SUPER="- Running super verification remove binary..."
  T_OPT_FINISH_VB="vendor_boot patch finished"
  T_OPT_FINISH_SUPER="Super verification removal finished!"
  T_OPT_SUPER_NOTE="【WARNING】Super patch includes vendor_boot patch. DO NOT modify system,system_dlkm,vendor partitions afterward!"
  T_OPT_SKIP="Extra patch skipped"
  T_BIN_FAIL="Binary execution failed!"

  T_VERIFY="- Verifying device model"
  T_DEVICE_OK="- Device verified:"
  T_PERM="- Setting permissions"
  T_EFISP_TITLE="Ensure kernel has no Baseband Guard and BL bootloader is unlocked"
  T_SOC="Ensure device is 8gen5 / 8elitegen5"
  T_CHECK_EXP="Detecting exploit..."
  T_INSTALL_CHOICE="Is this your first time installing Fake BL EFISP?"
  T_MODE_ASK="Select boot mode: Vol+ = Mode 1, Vol- = Mode 0 (Mode 2 can be set for this entry in the WebUI)"
  T_MODE_RECOVERY_WARN="Mode 1 requires grafting a custom recovery with the vbmeta tool first; without that graft data formatting cannot complete. Continue?"
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
  T_TZMAP_BIND_FAIL="TrustZone map does not match the ABL on flash; install rolled back"
  T_INACTIVE_SLOT_UNRESOLVED="Cannot resolve the inactive slot; skipping ABL pairing"
  T_INACTIVE_SLOT_VULN_SKIP="Inactive-slot ABL already has the GBL vulnerability; skipping pairing"
  T_INACTIVE_SLOT_PAIRING="Pairing the inactive-slot ABL"
  T_INACTIVE_SLOT_PAIR_WARN="Warning: inactive-slot ABL pairing failed; committed install retained"
  T_SETRW_FAIL="Failed to set efisp to read‑write"
  T_FLASH_FAIL="Failed to flash efisp"
  T_PERSIST_NOT_MOUNTED="persist is not mounted at /mnt/vendor/persist"
  T_EFISP_DIR_FAIL="efisp boot dir create failed"
  T_EFISP_WRITE_FAIL="efisp boot file write failed"
  T_PLACE_BOOT="Placing efisp boot entries on persist"
  T_FLASH_BDS="Flashing BDS to efisp"
  T_DONE_YES="Install complete. Reboot to recovery and format data, then reinstall module and choose NO"
  T_SEL_NO="Selected NO, skipping boot-chain writes and keeping the module WebUI"
  T_DONE_NO="Module install complete; reboot, then use the WebUI after each OTA"
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

BY_NAME_DIR=/dev/block/by-name
RUNTIME_DIR=$MODPATH/tmp
PERSIST_MNT=/mnt/vendor/persist
EFISP_DIR=$PERSIST_MNT/efisp
ABLREPO_URL="https://raw.githubusercontent.com/1vivy/gbl_root_canoe/main/ablrepo"
mkdir -p $RUNTIME_DIR

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
  if ! "$MODPATH/bin/extractfv" -o "$RUNTIME_DIR" -v "$abl_part" > "$RUNTIME_DIR/extract.log" 2>&1 ||
     ! "$MODPATH/bin/patch_abl" "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" > "$RUNTIME_DIR/patch.log" 2>&1 ||
     [ ! -s "$RUNTIME_DIR/patched.efi" ]; then
    ui_print "$T_PATCH_FAIL"
    return 1
  fi
  if grep -q "Warning: Failed to patch ABL GBL" "$RUNTIME_DIR/patch.log"; then
    CURRENT_PAIR_GBL_VULNERABLE=0
  fi
  if [ ! -x "$MODE2_PROFILE" ] ||
     ! "$MODE2_PROFILE" derive --vbmeta "$vbmeta_part" --out "$RUNTIME_DIR/boot.efi.gm2p" > "$RUNTIME_DIR/profile.log" 2>&1 ||
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



config_global_mode() {
  config_mode=$(awk '$1 == "mode" && $2 ~ /^[012]$/ { print $2; exit }' \
    "$1" 2>/dev/null)
  case "$config_mode" in 0|1|2) echo "$config_mode" ;; *) echo 1 ;; esac
}

config_entry_mode() {
  config_target="$1"
  config_id="$2"
  config_default="$3"
  config_mode=$(awk -v wanted="$config_id" '
    $1 == "entry" { in_entry = ($2 == wanted); next }
    in_entry && $1 == "mode" && $2 ~ /^[012]$/ { print $2; exit }
  ' "$config_target" 2>/dev/null)
  case "$config_mode" in 0|1|2) echo "$config_mode" ;; *) echo "$config_default" ;; esac
}

write_config_entry() {
  printf 'entry %s\n  title %s\n  image %s\n  mode %s\n  role %s\n\n' \
    "$1" "$2" "$3" "$4" "$5" >> "$6"
}

write_canoe_config() {
  config_output_dir="$1"
  requested_mode="$2"
  active_id=android-a
  active_title='Android (slot A)'
  [ "$current_slot_suffix" = "_b" ] && {
    active_id=android-b
    active_title='Android (slot B)'
  }
  fallback=$(config_global_mode "$config_output_dir/canoe.cfg")
  case "$requested_mode" in 0|1|2) fallback="$requested_mode" ;; *) return 1 ;; esac
  generation=$(awk '$1 == "generation" && $2 ~ /^[0-9]+$/ { print $2; exit }' \
    "$config_output_dir/canoe.cfg" 2>/dev/null)
  case "$generation" in ''|*[!0-9]*) generation=0 ;; esac
  [ "$generation" -lt 4294967295 ] && generation=$((generation + 1))
  config_output="$config_output_dir/canoe.cfg"
  temp="$config_output.tmp.$$"
  rm -f "$temp"
  active_mode=$(config_entry_mode "$config_output" "$active_id" "$fallback")
  {
    printf '# canoe.cfg - generated by canoe-device\n'
    printf 'version 1\n'
    printf 'generation %s\n' "$generation"
    printf 'timeout 5\n'
    printf 'default %s\n' "$active_id"
    printf 'mode %s\n' "$fallback"
    printf 'lockstate asneeded\n\n'
    write_config_entry "$active_id" "$active_title" boot.efi "$active_mode" active "$temp"
    if [ -f "$config_output_dir/boot_backup.efi" ]; then
      backup_mode=$(config_entry_mode "$config_output" android-backup "$fallback")
      write_config_entry android-backup 'Android (previous)' boot_backup.efi \
        "$backup_mode" backup "$temp"
    fi
  } > "$temp" || {
    rm -f "$temp"
    return 1
  }
  mv "$temp" "$config_output" && sync
}
PAIR_TXN_MARKER_NAME=".canoe.pair.txn"
PAIR_TXN_MARKER_MAGIC="CANOEP1"

pair_has_transaction_files() {
  target="$1"
  for pair_file in \
    "$target/.canoe.new.efi" "$target/.canoe.new.gm2p" "$target/.canoe.new.tzmap" \
    "$target/.canoe.old.live.efi" "$target/.canoe.old.live.gm2p" "$target/.canoe.old.live.tzmap" \
    "$target/.canoe.old.backup.efi" "$target/.canoe.old.backup.gm2p" "$target/.canoe.old.backup.tzmap" \
    "$target/.canoe.old.cfg" "$target/.canoe.old.cfg.absent" \
    "$target/$PAIR_TXN_MARKER_NAME" "$target/$PAIR_TXN_MARKER_NAME.tmp.$$"; do
    [ -e "$pair_file" ] && return 0
  done
  for pair_file in "$target/$PAIR_TXN_MARKER_NAME.tmp."*; do
    [ -e "$pair_file" ] && return 0
  done
  return 1
}

pair_new_cleanup() {
  target="$1"
  pair_cleanup_failed=0
  for pair_file in "$target/.canoe.new.efi" "$target/.canoe.new.gm2p" \
    "$target/.canoe.new.tzmap"; do
    if [ -e "$pair_file" ] && ! rm -f "$pair_file"; then
      pair_cleanup_failed=1
    fi
  done
  [ "$pair_cleanup_failed" = "0" ]
}

pair_transaction_cleanup() {
  target="$1"
  pair_cleanup_failed=0
  for pair_file in \
    "$target/.canoe.new.efi" "$target/.canoe.new.gm2p" "$target/.canoe.new.tzmap" \
    "$target/.canoe.old.live.efi" "$target/.canoe.old.live.gm2p" "$target/.canoe.old.live.tzmap" \
    "$target/.canoe.old.backup.efi" "$target/.canoe.old.backup.gm2p" "$target/.canoe.old.backup.tzmap" \
    "$target/.canoe.old.cfg" "$target/.canoe.old.cfg.absent"; do
    if [ -e "$pair_file" ] && ! rm -f "$pair_file"; then
      pair_cleanup_failed=1
    fi
  done
  for pair_file in "$target/$PAIR_TXN_MARKER_NAME.tmp."*; do
    if [ -e "$pair_file" ] && ! rm -f "$pair_file"; then
      pair_cleanup_failed=1
    fi
  done
  [ "$pair_cleanup_failed" = "0" ]
}

pair_marker_read() {
  target="$1"
  pair_marker_file="$target/$PAIR_TXN_MARKER_NAME"
  [ -f "$pair_marker_file" ] || return 1
  pair_marker_line=$(cat "$pair_marker_file" 2>/dev/null) || return 1
  case "$pair_marker_line" in
    CANOEP1'|prepared|'[01]'|'[01]'|'[01]'|'[01]'|'[01]'|'[01])
      pair_marker_state=prepared
      pair_marker_bits=${pair_marker_line#CANOEP1|prepared|}
      ;;
    CANOEP1'|committed|'[01]'|'[01]'|'[01]'|'[01]'|'[01]'|'[01])
      pair_marker_state=committed
      pair_marker_bits=${pair_marker_line#CANOEP1|committed|}
      ;;
    *) return 1 ;;
  esac
  pair_live_efi_bit=${pair_marker_bits%%|*}
  pair_marker_rest=${pair_marker_bits#*|}
  pair_live_profile_bit=${pair_marker_rest%%|*}
  pair_marker_rest=${pair_marker_rest#*|}
  pair_live_tzmap_bit=${pair_marker_rest%%|*}
  pair_marker_rest=${pair_marker_rest#*|}
  pair_backup_efi_bit=${pair_marker_rest%%|*}
  pair_marker_rest=${pair_marker_rest#*|}
  pair_backup_profile_bit=${pair_marker_rest%%|*}
  pair_backup_tzmap_bit=${pair_marker_rest#*|}
  return 0
}

pair_marker_write() {
  target="$1"
  state="$2"
  live_efi_bit="$3"
  live_profile_bit="$4"
  live_tzmap_bit="$5"
  backup_efi_bit="$6"
  backup_profile_bit="$7"
  backup_tzmap_bit="$8"
  pair_marker_file="$target/$PAIR_TXN_MARKER_NAME"
  pair_marker_tmp="$pair_marker_file.tmp.$$"
  if ! printf '%s|%s|%s|%s|%s|%s|%s|%s\n' "$PAIR_TXN_MARKER_MAGIC" \
      "$state" "$live_efi_bit" "$live_profile_bit" "$live_tzmap_bit" \
      "$backup_efi_bit" "$backup_profile_bit" "$backup_tzmap_bit" > "$pair_marker_tmp" ||
     ! mv "$pair_marker_tmp" "$pair_marker_file"; then
    rm -f "$pair_marker_tmp"
    return 1
  fi
  # A committed marker publishes the new pair. If durability sync fails after
  # that rename, preserve every old snapshot and leave the marker for startup
  # recovery; callers must not attempt rollback over the published pair.
  if ! sync; then
    [ "$state" = "committed" ] && return 2
    return 1
  fi
  return 0
}

pair_marker_clear() {
  target="$1"
  pair_marker_file="$target/$PAIR_TXN_MARKER_NAME"
  sync || return 1
  if [ -e "$pair_marker_file" ] && ! rm -f "$pair_marker_file"; then
    return 1
  fi
  sync || return 1
  return 0
}

pair_restore_one() {
  target="$1"
  canonical="$2"
  old_temp="$3"
  existed="$4"
  if [ "$existed" = "1" ]; then
    if [ -e "$old_temp" ]; then
      [ ! -e "$canonical" ] || rm -f "$canonical" || return 1
      mv "$old_temp" "$canonical" || return 1
    elif [ ! -e "$canonical" ]; then
      return 1
    fi
  else
    [ ! -e "$old_temp" ] || return 1
    [ ! -e "$canonical" ] || rm -f "$canonical" || return 1
  fi
  return 0
}

pair_recover() {
  target="$1"
  pair_marker_file="$target/$PAIR_TXN_MARKER_NAME"
  if [ ! -e "$pair_marker_file" ]; then
    pair_new_cleanup "$target" || return 1
    pair_has_transaction_files "$target" && return 1
    return 0
  fi
  pair_marker_read "$target" || return 1
  if [ "$pair_marker_state" = "committed" ]; then
    if [ -e "$target/.canoe.new.tzmap" ] &&
       [ ! -e "$target/boot.efi.tzmap" ] &&
       [ -e "$target/boot.efi" ] &&
       [ -e "$target/boot.efi.gm2p" ]; then
      mv "$target/.canoe.new.tzmap" "$target/boot.efi.tzmap" || return 1
    fi
    pair_transaction_cleanup "$target" || return 1
    sync || return 1
    pair_marker_clear "$target" || return 1
    return 0
  fi
  pair_restore_one "$target" "$target/boot.efi" \
    "$target/.canoe.old.live.efi" "$pair_live_efi_bit" || return 1
  pair_restore_one "$target" "$target/boot.efi.gm2p" \
    "$target/.canoe.old.live.gm2p" "$pair_live_profile_bit" || return 1
  pair_restore_one "$target" "$target/boot.efi.tzmap" \
    "$target/.canoe.old.live.tzmap" "$pair_live_tzmap_bit" || return 1
  pair_restore_one "$target" "$target/boot_backup.efi" \
    "$target/.canoe.old.backup.efi" "$pair_backup_efi_bit" || return 1
  pair_restore_one "$target" "$target/boot_backup.efi.gm2p" \
    "$target/.canoe.old.backup.gm2p" "$pair_backup_profile_bit" || return 1
  if [ -e "$target/.canoe.old.cfg" ]; then
    cp "$target/.canoe.old.cfg" "$target/canoe.cfg" || return 1
  elif [ -e "$target/.canoe.old.cfg.absent" ]; then
    rm -f "$target/canoe.cfg" || return 1
  fi
  pair_restore_one "$target" "$target/boot_backup.efi.tzmap" \
    "$target/.canoe.old.backup.tzmap" "$pair_backup_tzmap_bit" || return 1
  sync || return 1
  pair_transaction_cleanup "$target" || return 1
  sync || return 1
  pair_marker_clear "$target" || return 1
  return 0
}

pair_restore() {
  pair_recover "$1"
}

pair_begin() {
  target="$1"
  pair_recover "$target" || return 1
  pair_has_transaction_files "$target" && return 1
  pair_new_cleanup "$target" || return 1
  return 0
}

pair_prepare() {
  target="$1"
  pair_live_efi_bit=0
  pair_live_profile_bit=0
  pair_live_tzmap_bit=0
  pair_backup_efi_bit=0
  pair_backup_profile_bit=0
  pair_backup_tzmap_bit=0
  [ -e "$target/boot.efi" ] && pair_live_efi_bit=1
  [ -e "$target/boot.efi.gm2p" ] && pair_live_profile_bit=1
  [ -e "$target/boot.efi.tzmap" ] && pair_live_tzmap_bit=1
  [ -e "$target/boot_backup.efi" ] && pair_backup_efi_bit=1
  if [ -e "$target/canoe.cfg" ]; then
    cp "$target/canoe.cfg" "$target/.canoe.old.cfg" || return 1
    rm -f "$target/.canoe.old.cfg.absent"
  else
    rm -f "$target/.canoe.old.cfg"
    : > "$target/.canoe.old.cfg.absent" || return 1
  fi
  [ -e "$target/boot_backup.efi.gm2p" ] && pair_backup_profile_bit=1
  [ -e "$target/boot_backup.efi.tzmap" ] && pair_backup_tzmap_bit=1
  pair_marker_write "$target" prepared "$pair_live_efi_bit" \
    "$pair_live_profile_bit" "$pair_live_tzmap_bit" "$pair_backup_efi_bit" \
    "$pair_backup_profile_bit" "$pair_backup_tzmap_bit"
}

pair_install_failure() {
  target="$1"
  pair_restore "$target" || return 1
  return 1
}

install_pair() {
  target="$1"
  pair_begin "$target" || return 1
  cp "$RUNTIME_DIR/patched.efi" "$target/.canoe.new.efi" || {
    pair_new_cleanup "$target" || :
    return 1
  }
  cp "$RUNTIME_DIR/boot.efi.gm2p" "$target/.canoe.new.gm2p" || {
    pair_new_cleanup "$target" || :
    return 1
  }
  cp "$RUNTIME_DIR/boot.efi.tzmap" "$target/.canoe.new.tzmap" || {
    pair_new_cleanup "$target" || :
    return 1
  }
  if [ ! -s "$target/.canoe.new.efi" ] ||
     [ ! -s "$target/.canoe.new.gm2p" ] ||
     ! "$MODE2_PROFILE" validate --input "$target/.canoe.new.gm2p" >> "$RUNTIME_DIR/profile.log" 2>&1 ||
     [ ! -s "$target/.canoe.new.tzmap" ] ||
     ! "$ABL_TZMAP" validate "$target/.canoe.new.tzmap" >> "$RUNTIME_DIR/tzmap.log" 2>&1; then
    pair_new_cleanup "$target" || :
    return 1
  fi
  pair_prepare "$target" || {
    pair_new_cleanup "$target" || :
    return 1
  }
  if [ "$pair_live_efi_bit" = "1" ]; then
    mv "$target/boot.efi" "$target/.canoe.old.live.efi" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_live_profile_bit" = "1" ]; then
    mv "$target/boot.efi.gm2p" "$target/.canoe.old.live.gm2p" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_live_tzmap_bit" = "1" ]; then
    mv "$target/boot.efi.tzmap" "$target/.canoe.old.live.tzmap" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_backup_efi_bit" = "1" ]; then
    mv "$target/boot_backup.efi" "$target/.canoe.old.backup.efi" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_backup_profile_bit" = "1" ]; then
    mv "$target/boot_backup.efi.gm2p" "$target/.canoe.old.backup.gm2p" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_backup_tzmap_bit" = "1" ]; then
    mv "$target/boot_backup.efi.tzmap" "$target/.canoe.old.backup.tzmap" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_live_efi_bit" = "1" ]; then
    cp "$target/.canoe.old.live.efi" "$target/boot_backup.efi" || {
      pair_install_failure "$target"
      return 1
    }
  elif [ "$pair_backup_efi_bit" = "1" ]; then
    mv "$target/.canoe.old.backup.efi" "$target/boot_backup.efi" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ "$pair_live_efi_bit" = "1" ] && [ "$pair_live_profile_bit" = "1" ]; then
    cp "$target/.canoe.old.live.gm2p" "$target/boot_backup.efi.gm2p" || {
      pair_install_failure "$target"
      return 1
    }
  elif [ -e "$target/boot_backup.efi.gm2p" ] &&
       ! rm -f "$target/boot_backup.efi.gm2p"; then
    pair_install_failure "$target"
    return 1
  fi
  if [ "$pair_live_efi_bit" = "1" ] && [ "$pair_live_tzmap_bit" = "1" ]; then
    cp "$target/.canoe.old.live.tzmap" "$target/boot_backup.efi.tzmap" || {
      pair_install_failure "$target"
      return 1
    }
  elif [ -e "$target/boot_backup.efi.tzmap" ] &&
       ! rm -f "$target/boot_backup.efi.tzmap"; then
    pair_install_failure "$target"
    return 1
  fi
  mv "$target/.canoe.new.efi" "$target/boot.efi" || {
    pair_install_failure "$target"
    return 1
  }
  mv "$target/.canoe.new.gm2p" "$target/boot.efi.gm2p" || {
    pair_install_failure "$target"
    return 1
  }
  mv "$target/.canoe.new.tzmap" "$target/boot.efi.tzmap" || {
    pair_install_failure "$target"
    return 1
  }
  if [ -f "$MODPATH/efisp/BOOTENTRIES" ]; then
    cp "$MODPATH/efisp/BOOTENTRIES" "$target/" || {
      pair_install_failure "$target"
      return 1
    }
  fi
  if [ -d "$MODPATH/efisp/tools" ]; then
    if ! mkdir -p "$target/tools" ||
       ! cp -r "$MODPATH/efisp/tools/." "$target/tools/"; then
      pair_install_failure "$target"
      return 1
    fi
  fi
  if ! write_canoe_config "$target" "$selected_mode"; then
    pair_install_failure "$target"
    return 1
  fi
  if ! sync; then
    pair_install_failure "$target"
    return 1
  fi
  return 0
}

pair_commit() {
  target="$1"
  pair_marker_read "$target" || return 1
  [ "$pair_marker_state" = "prepared" ] || return 1
  sync || return 1
  pair_marker_write "$target" committed "$pair_live_efi_bit" \
    "$pair_live_profile_bit" "$pair_live_tzmap_bit" "$pair_backup_efi_bit" \
    "$pair_backup_profile_bit" "$pair_backup_tzmap_bit"
  pair_marker_status=$?
  case "$pair_marker_status" in
    0) ;;
    2) return 2 ;;
    *) return 1 ;;
  esac
  # Once the committed marker is durable, the new pair is irrevocable. Any
  # cleanup failure leaves the committed marker for startup recovery.
  pair_transaction_cleanup "$target" || return 0
  sync || return 0
  pair_marker_clear "$target" || return 0
  return 0
}


run_optional_patch() {
  if [ "$EXTRA_PATCH_MODE" = "vendor_boot" ]; then
    ui_print "$T_OPT_RUN_VB"
    ui_print "- 当前槽位: $slot_letter"
    if [ ! -x "$MODPATH/bin/patch_tools" ]; then
      ui_print "$T_BIN_FAIL: patch_tools binary not found!"
      abort "patch_tools missing"
    fi
    "$MODPATH/bin/patch_tools" patch_vendor "$slot_letter"
    ret=$?
    if [ "$ret" -ne 0 ]; then
      ui_print "$T_BIN_FAIL (vendor_boot ret:$ret)"
      abort "vendor_boot patch failed"
    fi
    ui_print "$T_OPT_FINISH_VB"
  elif [ "$EXTRA_PATCH_MODE" = "super" ]; then
    ui_print "$T_OPT_RUN_SUPER"
    ui_print "- 当前槽位: $slot_letter"
    if [ ! -x "$MODPATH/bin/patch_tools" ]; then
      ui_print "$T_BIN_FAIL: patch_tools binary not found!"
      abort "patch_tools missing"
    fi
    "$MODPATH/bin/patch_tools" patch_vendor "$slot_letter" super
    ret=$?
    if [ "$ret" -ne 0 ]; then
      ui_print "$T_BIN_FAIL (super ret:$ret)"
      abort "super patch failed"
    fi
    ui_print "$T_OPT_FINISH_SUPER"
    ui_print "$T_OPT_SUPER_NOTE"
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
        selected_mode=1
      fi
    fi

    if ! preflight_current_pair; then
      abort "ABL/vbmeta/profile preflight failed"
    fi
    if ! pair_recover "$EFISP_DIR"; then
      abort "pair transaction recovery failed before install"
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
      if ! pair_recover "$EFISP_DIR"; then
        abort "pair transaction recovery failed before ABL downgrade"
      fi
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
    if ! pair_recover "$EFISP_DIR"; then
      abort "pair transaction recovery failed before paired install"
    fi
    if ! grep -q " $PERSIST_MNT " /proc/mounts; then
      ui_print "$T_PERSIST_NOT_MOUNTED"
      abort "persist not mounted"
    fi
    mkdir -p "$EFISP_DIR" || { ui_print "$T_EFISP_DIR_FAIL"; abort "efisp mkdir failed"; }
    run_optional_patch
    if ! install_pair "$EFISP_DIR"; then
      if ! pair_restore "$EFISP_DIR"; then
        ui_print "$T_EFISP_WRITE_FAIL"
      fi
      ui_print "$T_EFISP_WRITE_FAIL"
      abort "efisp pair write failed"
    fi
    if ! "$ABL_TZMAP" verify --sidecar "$EFISP_DIR/boot.efi.tzmap" \
         --abl "$RUNTIME_DIR/LinuxLoader.efi" --allow-zero-digest \
         >> "$RUNTIME_DIR/tzmap.log" 2>&1; then
      pair_restore "$EFISP_DIR" || ui_print "$T_EFISP_WRITE_FAIL"
      ui_print "$T_TZMAP_BIND_FAIL"
      abort "ABL TrustZone map binding failed"
    fi

    ui_print "$T_FLASH_BDS"
    if ! blockdev --setrw "$BY_NAME_DIR/efisp" >> "$RUNTIME_DIR/flash.log" 2>&1 ||
       ! dd if="$MODPATH/BDS.efi" of="$BY_NAME_DIR/efisp" bs=4M conv=fsync \
         >> "$RUNTIME_DIR/flash.log" 2>&1 ||
       ! sync; then
      pair_restore "$EFISP_DIR" || ui_print "$T_EFISP_WRITE_FAIL"
      ui_print "$T_FLASH_FAIL"
      abort "BDS write failed"
    fi
    pair_commit "$EFISP_DIR"
    pair_commit_status=$?
    case "$pair_commit_status" in
      0) ;;
      2) abort "efisp pair committed; cleanup pending" ;;
      *)
        pair_restore "$EFISP_DIR" || ui_print "$T_EFISP_WRITE_FAIL"
        abort "efisp pair commit failed"
        ;;
    esac
    pair_inactive_abl
    ui_print "$T_DONE_YES"
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