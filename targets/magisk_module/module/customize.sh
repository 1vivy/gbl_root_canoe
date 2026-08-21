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

ui_print ""
ui_print "$T_OPT_MENU"
ui_print "$T_OPT_ASK"
ui_print "$T_OPT_UP_YES"
ui_print "$T_OPT_DOWN_SKIP"

EXTRA_PATCH_MODE=""
while true; do
  keyevent=$(read_volume_key)
  if [ "$keyevent" = "up" ]; then
    ui_print "$T_OPT_CHOICE1"
    ui_print "$T_OPT_VB"
    ui_print "$T_OPT_SUPER"
    while true; do
      keyevent2=$(read_volume_key)
      if [ "$keyevent2" = "up" ]; then
        EXTRA_PATCH_MODE="vendor_boot"
        break
      elif [ "$keyevent2" = "down" ]; then
        EXTRA_PATCH_MODE="super"
        break
      fi
    done
    break
  elif [ "$keyevent" = "down" ]; then
    EXTRA_PATCH_MODE="skip"
    ui_print "$T_OPT_SKIP"
    break
  fi
done

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
ABLREPO_URL="https://raw.githubusercontent.com/superturtlee/gbl_root_canoe/main/ablrepo"
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

# Fetch an older ABL with the GBL vulnerability. Looks up the local module
# bundle first, then the cloud. On success, leaves the image at
# $RUNTIME_DIR/repo_abl.img and returns 0; otherwise returns 1.
fetch_abl_from_repo() {
  product=$(getprop ro.product.name 2>/dev/null)
  [ -z "$product" ] && return 1
  local_dir="$MODPATH/ablrepo/$product"
  if [ -f "$local_dir/abl.img" ]; then
    if [ -f "$local_dir/abl.sha256" ] && verify_sha256 "$local_dir/abl.img" "$local_dir/abl.sha256"; then
      cp "$local_dir/abl.img" "$RUNTIME_DIR/repo_abl.img"
      ui_print "$T_ABLREPO_LOCAL"
      return 0
    fi
    ui_print "$T_ABLREPO_LOCAL_BAD"
  fi
  ui_print "$T_ABLREPO_CLOUD"
  if download_url "$ABLREPO_URL/$product/abl.sha256" "$RUNTIME_DIR/repo_abl.sha256" && \
     download_url "$ABLREPO_URL/$product/abl.img" "$RUNTIME_DIR/repo_abl.img"; then
    if verify_sha256 "$RUNTIME_DIR/repo_abl.img" "$RUNTIME_DIR/repo_abl.sha256"; then
      return 0
    fi
    ui_print "$T_ABLREPO_CLOUD_BAD"
  fi
  return 1
}
MODE2_PROFILE="$MODPATH/bin/mode2_profile"
ABL_TZMAP="$MODPATH/bin/abl_tzmap"
abl_part="$BY_NAME_DIR/abl$current_slot_suffix"
vbmeta_part="$BY_NAME_DIR/vbmeta$current_slot_suffix"

preflight_current_pair() {
  rm -f "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" \
    "$RUNTIME_DIR/patch.log" "$RUNTIME_DIR/boot.efi.gm2p" \
    "$RUNTIME_DIR/boot.efi.tzmap"
  if ! "$MODPATH/bin/extractfv" -o "$RUNTIME_DIR" -v "$abl_part" > "$RUNTIME_DIR/extract.log" 2>&1 ||
     ! "$MODPATH/bin/patch_abl" "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" > "$RUNTIME_DIR/patch.log" 2>&1 ||
     [ ! -s "$RUNTIME_DIR/patched.efi" ]; then
    ui_print "$T_PATCH_FAIL"
    return 1
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

read_preferred_mode() {
  [ -x "$MODE2_PROFILE" ] || return 1
  mode_partition_bytes=$(blockdev --getsize64 "$BY_NAME_DIR/efisp" 2>/dev/null) || return 1
  mode_block_size=$(blockdev --getss "$BY_NAME_DIR/efisp" 2>/dev/null) || return 1
  mode_result=$("$MODE2_PROFILE" mode-read --device "$BY_NAME_DIR/efisp" \
    --partition-bytes "$mode_partition_bytes" --block-size "$mode_block_size" 2>/dev/null) || return 1
  preferred_mode=$(printf '%s\n' "$mode_result" | sed -n 's/.*MODE=\([012]\).*/\1/p' | tail -n 1)
  preferred_defaulted=$(printf '%s\n' "$mode_result" | sed -n 's/.*MODE_DEFAULTED=\([01]\).*/\1/p' | tail -n 1)
  case "$preferred_mode:$preferred_defaulted" in
    0:0|1:0|2:0|0:1|1:1|2:1) return 0 ;;
    *) return 1 ;;
  esac
}
write_preferred_mode() {
  mode_value="$1"
  "$MODE2_PROFILE" mode-write --device "$BY_NAME_DIR/efisp" \
    --partition-bytes "$mode_partition_bytes" --block-size "$mode_block_size" \
    --mode "$mode_value" >> "$RUNTIME_DIR/flash.log" 2>&1
}

restore_preferred_mode() {
  mode_value="$1"
  restore_mode_ok=1
  write_preferred_mode "$mode_value" || restore_mode_ok=0
  if ! read_preferred_mode; then
    restore_mode_ok=0
  elif [ "$preferred_mode" != "$mode_value" ] ||
       [ "$preferred_defaulted" != "0" ]; then
    restore_mode_ok=0
  fi
  [ "$restore_mode_ok" = "1" ]
}


PAIR_TXN_MARKER_NAME=".canoe.pair.txn"
PAIR_TXN_MARKER_MAGIC="CANOEP1"

pair_has_transaction_files() {
  target="$1"
  for pair_file in \
    "$target/.canoe.new.efi" "$target/.canoe.new.gm2p" "$target/.canoe.new.tzmap" \
    "$target/.canoe.old.live.efi" "$target/.canoe.old.live.gm2p" "$target/.canoe.old.live.tzmap" \
    "$target/.canoe.old.backup.efi" "$target/.canoe.old.backup.gm2p" "$target/.canoe.old.backup.tzmap" \
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
    "$target/.canoe.old.backup.efi" "$target/.canoe.old.backup.gm2p" "$target/.canoe.old.backup.tzmap"; do
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
    if ! preflight_current_pair; then
      abort "ABL/vbmeta/profile preflight failed"
    fi
    if ! read_preferred_mode; then
      abort "preferred mode read failed"
    fi
    if ! pair_recover "$EFISP_DIR"; then
      abort "pair transaction recovery failed before optional patch"
    fi
    if grep -q "Warning: Failed to patch ABL GBL" "$RUNTIME_DIR/patch.log"; then
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
      ui_print "$T_ABLREPO_DOWNGRADE"
      if ! pair_recover "$EFISP_DIR"; then
        abort "pair transaction recovery failed before ABL downgrade"
      fi

      if ! blockdev --setrw "$abl_part" >> "$RUNTIME_DIR/flash.log" 2>&1; then
        ui_print "$T_ABL_SETRW_FAIL"
        abort "setrw abl failed"
      fi
      if ! dd if="$RUNTIME_DIR/repo_abl.img" of="$abl_part" bs=4M conv=fsync >> "$RUNTIME_DIR/flash.log" 2>&1; then
        ui_print "$T_ABL_FLASH_FAIL"
        abort "downgrade abl failed"
      fi
      if ! sync; then
        ui_print "$T_ABL_FLASH_FAIL"
        abort "sync downgraded abl failed"
      fi
      ui_print "$T_ABLREPO_OK"
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
    # All abort-prone boot-chain prerequisites passed; only now mutate optional
    # partitions, immediately before committing the paired efisp install.
    run_optional_patch
    if ! install_pair "$EFISP_DIR"; then
      if ! pair_restore "$EFISP_DIR"; then
        ui_print "$T_EFISP_WRITE_FAIL"
      fi
      ui_print "$T_EFISP_WRITE_FAIL"
      abort "efisp pair write failed"
    fi

    ui_print "$T_FLASH_BDS"
    if ! blockdev --setrw "$BY_NAME_DIR/efisp" >> "$RUNTIME_DIR/flash.log" 2>&1; then
      if ! pair_restore "$EFISP_DIR"; then
        ui_print "$T_EFISP_WRITE_FAIL"
      fi
      ui_print "$T_SETRW_FAIL"
      abort "setrw failed"
    fi
    if ! dd if="$MODPATH/BDS.efi" of="$BY_NAME_DIR/efisp" bs=4M conv=fsync >> "$RUNTIME_DIR/flash.log" 2>&1; then
      if ! pair_restore "$EFISP_DIR"; then
        ui_print "$T_EFISP_WRITE_FAIL"
      fi
      ui_print "$T_FLASH_FAIL"
      abort "flash failed"
    fi
    if ! sync; then
      if ! pair_restore "$EFISP_DIR"; then
        ui_print "$T_EFISP_WRITE_FAIL"
      fi
      ui_print "$T_FLASH_FAIL"
      abort "sync BDS failed"
    fi
    wanted_mode="$preferred_mode"
    mode_write_ok=1
    if ! write_preferred_mode "$wanted_mode"; then
      mode_write_ok=0
    fi
    mode_read_ok=1
    if ! read_preferred_mode; then
      mode_read_ok=0
    fi
    if [ "$mode_write_ok" != "1" ] ||
       [ "$mode_read_ok" != "1" ] ||
       [ "$preferred_mode" != "$wanted_mode" ] ||
       [ "$preferred_defaulted" != "0" ]; then
      mode_restore_ok=1
      restore_preferred_mode "$wanted_mode" || mode_restore_ok=0
      if ! pair_restore "$EFISP_DIR"; then
        ui_print "$T_EFISP_WRITE_FAIL"
      fi
      [ "$mode_restore_ok" = "1" ] || ui_print "$T_EFISP_WRITE_FAIL"
      abort "preferred mode write failed"
    fi
    pair_commit "$EFISP_DIR"
    pair_commit_status=$?
    case "$pair_commit_status" in
      0) ;;
      2)
        abort "efisp pair committed; cleanup pending"
        ;;
      *)
        if ! pair_restore "$EFISP_DIR"; then
          ui_print "$T_EFISP_WRITE_FAIL"
        fi
        abort "efisp pair commit failed"
        ;;
    esac
    ui_print "$T_DONE_YES"
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