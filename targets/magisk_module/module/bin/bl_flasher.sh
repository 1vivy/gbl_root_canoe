#!/system/bin/sh
if [ -z "$MODDIR" ]; then
  MODDIR=$(CDPATH= cd -- "$(dirname "$0")/.." 2>/dev/null && pwd)
fi
if [ -z "$MODDIR" ]; then
  echo 'ERROR=MODDIR detection failed' >&2
  exit 1
fi

export MODPATH=$MODDIR
export BINDIR=$MODDIR/bin

LANG=zh
if [ -f "$MODDIR/lang.txt" ]; then
  USER_LANG=$(tr -d '[:space:]' < "$MODDIR/lang.txt")
  if [ "$USER_LANG" = "en" ]; then
    LANG=en
  fi
fi

if [ "$LANG" = "zh" ]; then
  TEXT_IDLE="等待操作"
  TEXT_NO_SLOT="无法识别当前槽位"
  TEXT_NO_TARGET_SLOT="无法解析 OTA 目标槽位；bootctl/GPT 元数据不可用，拒绝将运行槽位重新标记"
  TEXT_FLASHING="刷写任务运行中，目标槽位"
  TEXT_PATCH_ONLY="分区修补任务运行中"
  TEXT_DEBUG_MODE="调试模式：仅处理不刷写，efisp 目录使用模块 tmp/efisp"
  TEXT_DEBUG_DONE="调试任务已完成，文件保存在"
  TEXT_DEBUG_FAILED="调试过程中出错"
  TEXT_EXTRACT_FAILED="ABL 提取失败"
  TEXT_PATCH_FAILED="补丁应用失败"
  TEXT_PERSIST_NOT_MOUNTED="persist 分区未挂载到 /mnt/vendor/persist"
  TEXT_EFISP_MKDIR_FAILED="创建 efisp 启动目录失败"
  TEXT_EFISP_WRITE_FAILED="写入 efisp 启动文件失败"
  TEXT_BACKUP_BOOT="已备份旧的 boot.efi"
  TEXT_EFISP_FILES_OK="efisp 启动项已更新"
  TEXT_EFISP_FLASH_FAILED="efisp 刷写失败"
  TEXT_EFISP_FLASH_OK="efisp 刷写完成"
  TEXT_UPDATING_BDS_TOOLS="BDS 与 Tools 更新任务运行中"
  TEXT_BDS_TOOLS_OK="BDS 与 Tools 更新任务已完成"
  TEXT_BDS_TOOLS_FAIL="BDS 与 Tools 更新失败"
  TEXT_BDS_OLD_VER="当前 efisp 使用旧版布局，请先通过完整安装流程部署修补后的 ABL/profile pair"
  TEXT_BDS_NOT_INSTALLED="尚未安装修补后的 ABL/profile pair，请重新安装模块并选择全新安装"
  TEXT_GBL_VULN="检测到GBL漏洞，跳过BL刷写"
  TEXT_GBL_VULN_SKIP="已跳过BL刷写"
  TEXT_GBL_DETECT_FAILED="漏洞检测失败，继续流程"
  TEXT_NO_GBL_VULN="未检测到GBL漏洞"
  TEXT_EFISP_WARN="efisp 刷写失败，继续刷入BL"
  TEXT_SET_RW_FAILED="分区设置可写失败"
  TEXT_FLASH_PART="刷写"
  TEXT_FLASH_OK="完成"
  TEXT_ALL_OK="刷写任务已完成（含 efisp）"
  TEXT_ALL_OK_NO_EFISP="分区修补任务已完成（未刷写 ABL）"
  TEXT_BUSY="任务正在运行"
  TEXT_LOG_CLEARED="日志已清空"
  TEXT_PATCH_START="分区修补任务运行中"
  TEXT_PATCH_VENDORBOOT_START="修补 vendor_boot"
  TEXT_PATCH_VENDORBOOT_DONE="vendor_boot 修补完成"
  TEXT_PATCH_DEBUG_SAVE="调试模式：跳过实际刷写"
  TEXT_PATCH_NO_SELECTED="未勾选任何需要修补的分区"
  TEXT_SIGNER_CHANGED="vbmeta 签名者已变化。这在切换到或离开自定义 ROM 时是预期的；此处没有任何工具能证明哪个密钥才是 OEM 的。Mode 2 已降级为 Mode 1。"
  TEXT_PATCH_ERR="分区修补出错"
  TEXT_PATCH_DONE="分区修补任务已完成"
  TEXT_BIN_NOT_FOUND="修补文件未找到"
  TEXT_PATCH_ARGS="修补参数"
  TEXT_BIN_RUN_INFO="执行中"
  TEXT_PATCH_SLOT="目标槽位"
else
  TEXT_NO_SLOT="Cannot detect current slot"
  TEXT_NO_TARGET_SLOT="Cannot resolve the OTA target slot; bootctl/GPT metadata is unavailable, refusing to relabel the running slot"
  TEXT_FLASHING="Flash task running, target slot"
  TEXT_PATCH_ONLY="Partition patch task running"
  TEXT_DEBUG_DONE="Debug task completed"
  TEXT_DEBUG_FAILED="Debug error"
  TEXT_EXTRACT_FAILED="ABL extract failed"
  TEXT_PATCH_FAILED="Patch failed"
  TEXT_PERSIST_NOT_MOUNTED="persist is not mounted at /mnt/vendor/persist"
  TEXT_EFISP_MKDIR_FAILED="efisp boot dir create failed"
  TEXT_EFISP_WRITE_FAILED="efisp boot file write failed"
  TEXT_BACKUP_BOOT="Backed up old boot.efi"
  TEXT_EFISP_FILES_OK="efisp boot entries updated"
  TEXT_EFISP_FLASH_FAILED="efisp flash failed"
  TEXT_EFISP_FLASH_OK="efisp flash ok"
  TEXT_UPDATING_BDS_TOOLS="BDS and Tools update task running"
  TEXT_BDS_TOOLS_OK="BDS and Tools update task completed"
  TEXT_BDS_TOOLS_FAIL="BDS and Tools update failed"
  TEXT_BDS_OLD_VER="The efisp partition uses the legacy layout. Run the full install flow for the patched ABL/profile pair first."
  TEXT_BDS_NOT_INSTALLED="No patched ABL/profile pair is installed. Reinstall the module and choose a fresh install."
  TEXT_GBL_VULN="GBL vuln detected, skip BL flash"
  TEXT_GBL_VULN_SKIP="Skipped BL flash"
  TEXT_GBL_DETECT_FAILED="Vuln check failed"
  TEXT_NO_GBL_VULN="No GBL vuln found"
  TEXT_EFISP_WARN="efisp failed, continue BL"
  TEXT_SET_RW_FAILED="setrw failed"
  TEXT_FLASH_PART="Flashing"
  TEXT_FLASH_OK="done"
  TEXT_ALL_OK="Flash task completed (with efisp)"
  TEXT_ALL_OK_NO_EFISP="Partition patch task completed (ABL not flashed)"
  TEXT_BUSY="Task running"
  TEXT_LOG_CLEARED="Log cleared"
  TEXT_PATCH_START="Partition patch task running"
  TEXT_PATCH_VENDORBOOT_START="Patch vendor_boot"
  TEXT_PATCH_VENDORBOOT_DONE="vendor_boot patched"
  TEXT_PATCH_DEBUG_SAVE="Debug mode: skip actual flash"
  TEXT_SIGNER_CHANGED="The vbmeta signer changed. This is expected when moving to or from a custom ROM; no tool here can prove which key is the OEM's. Mode 2 was downgraded to Mode 1."
  TEXT_PATCH_NO_SELECTED="No partition selected for patching"
  TEXT_PATCH_ERR="Partition patch error"
  TEXT_PATCH_DONE="Partition patch task completed"
  TEXT_BIN_NOT_FOUND="Binary not found"
  TEXT_PATCH_ARGS="Patch args"
  TEXT_BIN_RUN_INFO="Run binary"
  TEXT_PATCH_SLOT="Target slot"
fi

RUNTIME_DIR="${RUNTIME_DIR:-$MODDIR/tmp}"
BY_NAME_DIR="${BY_NAME_DIR:-/dev/block/by-name}"
PERSIST_MNT="${PERSIST_MNT:-/mnt/vendor/persist}"
EFISP_DIR="${EFISP_DIR:-$PERSIST_MNT/efisp}"
BDS_EFI="$MODDIR/BDS.efi"
CANOE_BOOTMGR="$BINDIR/canoe-bootmgr"
IMAGE_NAMES="abl"
LOG_FILE="$RUNTIME_DIR/flash.log"
STATE_FILE="$RUNTIME_DIR/state"
MESSAGE_FILE="$RUNTIME_DIR/message"
UPDATED_FILE="$RUNTIME_DIR/updated"
TASK_FILE="$RUNTIME_DIR/task_id"
SUPPLIED_DIR="${SUPPLIED_DIR:-/data/local/tmp/canoe}"
SIGNER_SOURCE=partition
PID_FILE="$RUNTIME_DIR/flash.pid"
LOCK_DIR="$RUNTIME_DIR/flash.lock"
LOCK_OWNER_FILE="$LOCK_DIR/owner.pid"
LOCK_TASK_FILE="$LOCK_DIR/task_id"
export PATH=/data/adb/ksu/bin:/system/bin:/system/xbin:$PATH
RUNTIME_READY=0

timestamp() { date '+%Y-%m-%d %H:%M:%S'; }
read_line() { [ -f "$1" ] && IFS= read -r _line < "$1" && printf "%s\n" "$_line"; }
emit() { printf "%s\n" "$1"; }

ensure_runtime() {
  [ "$RUNTIME_READY" = "1" ] && return
  mkdir -p "$RUNTIME_DIR"
  [ -f "$LOG_FILE" ] || : > "$LOG_FILE"
  [ -f "$STATE_FILE" ] || echo idle > "$STATE_FILE"
  [ -f "$MESSAGE_FILE" ] || echo "$TEXT_IDLE" > "$MESSAGE_FILE"
  [ -f "$UPDATED_FILE" ] || timestamp > "$UPDATED_FILE"
  [ -f "$TASK_FILE" ] || echo 0 > "$TASK_FILE"
  RUNTIME_READY=1
}

clean_workdir() {
  for _f in "$RUNTIME_DIR"/*; do
    [ -e "$_f" ] || continue
    case "${_f##*/}" in
      flash.pid|state|message|updated|task_id|flash.log|flash.lock) ;;
      *) rm -rf "$_f" ;;
    esac
  done
}

write_state() {
  ensure_runtime
  echo "$1" > "$STATE_FILE"
  echo "$2" > "$MESSAGE_FILE"
  timestamp > "$UPDATED_FILE"
}

write_log() {
  ensure_runtime
  echo "[$(timestamp)] $*" >> "$LOG_FILE"
}

detect_current_slot() {
  case "$(getprop ro.boot.slot_suffix 2>/dev/null)" in
    _a) echo _a ;;
    _b) echo _b ;;
    *) return 1 ;;
  esac
}

other_slot() {
  case "$1" in
    _a) echo _b ;;
    _b) echo _a ;;
    *) return 1 ;;
  esac
}

slot_suffix_to_letter() {
  echo "${1#_}"
}
next_boot_slot() {
  bootctl_path=/data/adb/ksu/bin/bootctl
  if [ ! -x "$bootctl_path" ]; then
    bootctl_path=$(command -v bootctl 2>/dev/null || :)
  fi
  [ -x "$bootctl_path" ] || return 1
  boot_slot=$("$bootctl_path" get-active-boot-slot 2>/dev/null) || return 1
  case "$boot_slot" in
    0) echo _a ;;
    1) echo _b ;;
    *) return 1 ;;
  esac
}

partition_path() { echo "$BY_NAME_DIR/$1$2"; }

pid_from_file() {
  pid_file="$1"
  [ -f "$pid_file" ] || return 1
  pid_value=$(tr -d '[:space:]' < "$pid_file")
  case "$pid_value" in
    ''|*[!0-9]*) rm -f "$pid_file"; return 1 ;;
  esac
  if kill -0 "$pid_value" 2>/dev/null; then
    echo "$pid_value"
    return 0
  fi
  rm -f "$pid_file"
  return 1
}

current_pid() { pid_from_file "$PID_FILE"; }
lock_owner_pid() { pid_from_file "$LOCK_OWNER_FILE"; }

task_busy() {
  current_pid >/dev/null && return 0
  [ -d "$LOCK_DIR" ] || return 1
  if [ ! -f "$LOCK_OWNER_FILE" ]; then
    # mkdir is the atomic lock operation, but owner publication follows it.
    # Give a live creator one bounded grace period; an ownerless directory
    # after that can only be an interrupted acquisition and is safe to reap.
    sleep 1
    [ -d "$LOCK_DIR" ] || return 1
    if [ ! -f "$LOCK_OWNER_FILE" ]; then
      rm -f "$LOCK_TASK_FILE" "$LOCK_OWNER_FILE".tmp.* \
        "$LOCK_TASK_FILE".tmp.*
      rmdir "$LOCK_DIR" 2>/dev/null || return 0
      return 1
    fi
  fi
  lock_owner_pid >/dev/null && return 0
  rm -f "$LOCK_OWNER_FILE" "$LOCK_TASK_FILE" \
    "$LOCK_OWNER_FILE".tmp.* "$LOCK_TASK_FILE".tmp.*
  rmdir "$LOCK_DIR" 2>/dev/null || return 0
  return 1
}

write_atomic_value() {
  atomic_target="$1"
  atomic_value="$2"
  atomic_temp="${atomic_target}.tmp.$$"
  if ! printf '%s\n' "$atomic_value" > "$atomic_temp" ||
     ! mv "$atomic_temp" "$atomic_target"; then
    rm -f "$atomic_temp"
    return 1
  fi
  return 0
}

acquire_task_lock() {
  lock_task_id="$1"
  if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    task_busy && return 1
    mkdir "$LOCK_DIR" 2>/dev/null || return 1
  fi
  if ! write_atomic_value "$LOCK_OWNER_FILE" "$$" ||
     ! write_atomic_value "$LOCK_TASK_FILE" "$lock_task_id"; then
    rm -f "$LOCK_OWNER_FILE" "$LOCK_TASK_FILE" \
      "$LOCK_OWNER_FILE".tmp.* "$LOCK_TASK_FILE".tmp.*
    rmdir "$LOCK_DIR" 2>/dev/null || :
    return 1
  fi
  return 0
}

claim_worker_lock() {
  worker_task_id="$1"
  [ -d "$LOCK_DIR" ] || return 1
  [ "$(read_line "$LOCK_TASK_FILE")" = "$worker_task_id" ] || return 1
  write_atomic_value "$LOCK_OWNER_FILE" "$$" || return 1
  if ! write_atomic_value "$PID_FILE" "$$"; then
    cleanup_lock
    return 1
  fi
  trap cleanup_lock EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  trap 'exit 129' HUP
  return 0
}

cleanup_lock() {
  [ "$(read_line "$LOCK_OWNER_FILE")" = "$$" ] || return
  rm -f "$PID_FILE" "$LOCK_OWNER_FILE" "$LOCK_TASK_FILE" \
    "$LOCK_OWNER_FILE".tmp.* "$LOCK_TASK_FILE".tmp.* "$PID_FILE".tmp.*
  rmdir "$LOCK_DIR" 2>/dev/null || :
}

persist_mounted() { grep -q " $PERSIST_MNT " /proc/mounts; }


mode2_profile_path() { echo "$BINDIR/mode2_profile"; }
abl_tzmap_path() { echo "$BINDIR/abl_tzmap"; }

build_abl_tzmap() {
  abl="$1"
  output="$2"
  tool=$(abl_tzmap_path)
  rm -f "$output"
  [ -x "$tool" ] || return 1
  [ -e "$abl" ] || return 1
  # --allow-incomplete: an ABL with no recorded RE evidence still gets a sidecar
  # carrying the soundly derived identifier flags.
  "$tool" derive "$abl" -o "$output" --allow-incomplete >> "$LOG_FILE" 2>&1 || {
    rm -f "$output"
    return 1
  }
  [ -s "$output" ] || {
    rm -f "$output"
    return 1
  }
  "$tool" validate "$output" >> "$LOG_FILE" 2>&1 || {
    rm -f "$output"
    return 1
  }
  return 0
}

build_mode2_profile() {
  vbmeta="$1"
  output="$2"
  tool=$(mode2_profile_path)
  rm -f "$output"
  [ -x "$tool" ] || return 1
  [ -e "$vbmeta" ] || return 1
  "$tool" derive --vbmeta "$vbmeta" --out "$output" >> "$LOG_FILE" 2>&1 || {
    rm -f "$output"
    return 1
  }
  [ -s "$output" ] || {
    rm -f "$output"
    return 1
  }
  "$tool" validate --input "$output" >> "$LOG_FILE" 2>&1 || {
    rm -f "$output"
    return 1
  }
  return 0
}



config_global_mode() {
  config_target="$1"
  config_mode=$(awk '$1 == "mode" && $2 ~ /^[012]$/ { print $2; exit }' \
    "$config_target" 2>/dev/null)
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

config_active_id() {
  case "$1" in
    _a) echo android-a ;;
    _b) echo android-b ;;
    *) echo android-a ;;
  esac
}

config_mode_for_slot() {
  config_target="$1"
  config_slot="$2"
  config_id=$(config_active_id "$config_slot")
  [ -f "$config_target" ] || return 1
  config_fallback=$(config_global_mode "$config_target")
  config_mode=$(config_entry_mode "$config_target" "$config_id" "$config_fallback")
  config_explicit=$(awk -v wanted="$config_id" '
    $1 == "entry" { in_entry = ($2 == wanted); next }
    in_entry && $1 == "mode" && $2 ~ /^[012]$/ { print 1; exit }
  ' "$config_target" 2>/dev/null)
  case "$config_explicit" in
    1) CONFIG_MODE_DEFAULTED=0 ;;
    *) CONFIG_MODE_DEFAULTED=1 ;;
  esac
  PREFERRED_MODE="$config_mode"
  MODE_DEFAULTED="$CONFIG_MODE_DEFAULTED"
  return 0
}

install_efisp_pair() {
  target="$1"
  source_efi="$2"
  source_profile="$3"
  source_tzmap="$4"
  active_slot="$5"
  flash_bds="${6:-yes}"
  case "$active_slot" in _a|_b) ;; *) return 1 ;; esac
  install_mode=1
  if config_mode_for_slot "$target/canoe.cfg" "$active_slot"; then
    install_mode="$PREFERRED_MODE"
  fi
  active_slot_letter=$(slot_suffix_to_letter "$active_slot")
  stage="$target/.canoe.stage.$$"
  rm -rf "$stage"
  mkdir -p "$stage" || return 1
  if ! cp "$source_efi" "$stage/boot.efi" ||
     ! cp "$source_profile" "$stage/boot.efi.gm2p" ||
     ! cp "$source_tzmap" "$stage/boot.efi.tzmap"; then
    rm -rf "$stage"
    return 1
  fi
  if [ "$flash_bds" = "yes" ] && ! cp "$BDS_EFI" "$stage/BDS.efi"; then
    rm -rf "$stage"
    return 1
  fi
  if [ -d "$MODDIR/efisp/tools" ] &&
     { ! mkdir -p "$stage/tools" ||
       ! cp -r "$MODDIR/efisp/tools/." "$stage/tools/"; }; then
    rm -rf "$stage"
    return 1
  fi
  if ! "$(abl_tzmap_path)" verify --sidecar "$stage/boot.efi.tzmap" \
       --abl "$RUNTIME_DIR/LinuxLoader.efi" --allow-zero-digest \
       >> "$LOG_FILE" 2>&1; then
    rm -rf "$stage"
    return 1
  fi
  [ -x "$CANOE_BOOTMGR" ] || {
    write_log "$TEXT_BIN_NOT_FOUND: $CANOE_BOOTMGR"
    rm -rf "$stage"
    return 1
  }
  transaction_log="$RUNTIME_DIR/transaction.log"
  rm -f "$transaction_log"
  if "$CANOE_BOOTMGR" --json --boot-root "$target" install \
      --staged "$stage" --slot "$active_slot_letter" \
      --active-slot "$active_slot_letter" --mode "$install_mode" \
      --allow-new-signer > "$transaction_log" 2>&1; then
    transaction_status=0
  else
    transaction_status=$?
  fi
  cat "$transaction_log" >> "$LOG_FILE"
  if [ "$transaction_status" -ne 0 ]; then
    rm -rf "$stage"
    return 1
  fi
  if [ "$flash_bds" = "yes" ]; then
    if ! cp "$stage/BDS.efi" "$target/BDS.efi" ||
       { [ -d "$stage/tools" ] &&
         ! mkdir -p "$target/tools"; } ||
       { [ -d "$stage/tools" ] &&
         ! cp -r "$stage/tools/." "$target/tools/"; }; then
      rm -rf "$stage"
      return 1
    fi
  fi
  if grep -q '"signer_changed":true' "$transaction_log" &&
     [ "$SIGNER_SOURCE" != "supplied" ]; then
    write_log "$TEXT_SIGNER_CHANGED"
    if [ "$install_mode" = "2" ]; then
      entry_id=$(config_active_id "$active_slot")
      if ! "$CANOE_BOOTMGR" --boot-root "$target" entry mode \
           --id "$entry_id" --mode 1 >> "$LOG_FILE" 2>&1; then
        rm -rf "$stage"
        return 1
      fi
      write_log "Mode 2 downgraded to Mode 1 after signer change"
    fi
  fi
  rm -rf "$stage"
  return 0
}

build_patched_efi() {
  abl="$1"
  rm -f "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" "$RUNTIME_DIR/patch.log"
  if ! "$MODDIR/bin/extractfv" -o "$RUNTIME_DIR" -v "$abl" >> "$LOG_FILE" 2>&1; then
    write_log "$TEXT_EXTRACT_FAILED"
    return 1
  fi
  if ! "$MODDIR/bin/patch_abl" "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi" > "$RUNTIME_DIR/patch.log" 2>&1; then
    cat "$RUNTIME_DIR/patch.log" >> "$LOG_FILE"
    write_log "$TEXT_PATCH_FAILED"
    return 1
  fi
  cat "$RUNTIME_DIR/patch.log" >> "$LOG_FILE"
  [ -s "$RUNTIME_DIR/patched.efi" ] || { write_log "$TEXT_PATCH_FAILED"; return 1; }
}

update_efisp() {
  target_abl="$1"
  target_vbmeta="$2"
  is_debug="$3"
  source_abl="$4"
  source_vbmeta="$5"
  active_slot="${6:-_a}"
  clean_workdir
  build_patched_efi "$target_abl" || return 3
  if grep -q "Warning: Failed to patch ABL GBL" "$RUNTIME_DIR/patch.log"; then
    gbl_vuln=0
  else
    gbl_vuln=1
  fi

  # A non-vulnerable target is replaced by the current slot below, so derive
  # every efisp artifact from that same current-slot pair. Debug calls omit a
  # replacement source and retain the inspected target pair.
  if [ "$gbl_vuln" = "1" ] || [ -z "$source_abl" ]; then
    source_patched_efi="$RUNTIME_DIR/patched.efi"
    source_vbmeta="$target_vbmeta"
  else
    [ -n "$source_vbmeta" ] || source_vbmeta="$target_vbmeta"
    build_patched_efi "$source_abl" || return 3
    source_patched_efi="$RUNTIME_DIR/patched.efi"
  fi
  if ! build_mode2_profile "$source_vbmeta" "$RUNTIME_DIR/patched.efi.gm2p"; then
    write_log "Mode 2 profile build failed"
    return 3
  fi

  if ! build_abl_tzmap "$RUNTIME_DIR/LinuxLoader.efi" "$RUNTIME_DIR/patched.efi.tzmap"; then
    write_log "ABL TrustZone map build failed"
    return 3
  fi
  if [ "$is_debug" = "yes" ]; then
    write_log "$TEXT_DEBUG_MODE"
    efisp_target=$RUNTIME_DIR/efisp
    mkdir -p "$efisp_target" >> "$LOG_FILE" 2>&1 ||
      { write_log "$TEXT_EFISP_MKDIR_FAILED"; return 1; }
    if ! install_efisp_pair "$efisp_target" "$source_patched_efi" \
         "$RUNTIME_DIR/patched.efi.gm2p" "$RUNTIME_DIR/patched.efi.tzmap" \
         "$active_slot" no; then
      write_log "$TEXT_EFISP_WRITE_FAILED"
      return 1
    fi
    write_log "$TEXT_EFISP_FILES_OK"
    return 0
  fi
  efisp_target=$EFISP_DIR
  if ! persist_mounted; then
    write_log "$TEXT_PERSIST_NOT_MOUNTED"
    return 1
  fi
  mkdir -p "$efisp_target" >> "$LOG_FILE" 2>&1 ||
    { write_log "$TEXT_EFISP_MKDIR_FAILED"; return 1; }
  if ! install_efisp_pair "$efisp_target" "$source_patched_efi" \
       "$RUNTIME_DIR/patched.efi.gm2p" "$RUNTIME_DIR/patched.efi.tzmap" \
       "$active_slot"; then
    write_log "$TEXT_EFISP_WRITE_FAILED"
    return 1
  fi
  write_log "$TEXT_EFISP_FILES_OK"
  if [ "$gbl_vuln" = "1" ]; then
    write_log "$TEXT_GBL_VULN"
    return 2
  fi
  return 0
}

detect_gbl_vulnerability() {
  clean_workdir
  build_patched_efi "$1" || return 1
  if ! grep -q "Warning: Failed to patch ABL GBL" $RUNTIME_DIR/patch.log; then
    write_log "$TEXT_GBL_VULN"
    return 0
  fi
  write_log "$TEXT_NO_GBL_VULN"
  return 2
}

efisp_has_mz() {
  [ -b "$BY_NAME_DIR/efisp" ] || return 1
  [ "$(dd if="$BY_NAME_DIR/efisp" bs=2 count=1 2>/dev/null)" = "MZ" ]
}

gbl_exploit_present() {
  current_slot=$(detect_current_slot) || return 1
  abl=$(partition_path abl "$current_slot")
  detect_gbl_vulnerability "$abl"
  [ $? -eq 0 ]
}

update_bds_tools() {
  if ! persist_mounted; then
    write_log "$TEXT_PERSIST_NOT_MOUNTED"
    return 1
  fi
  bds_pair_ready=1
  [ -s "$EFISP_DIR/boot.efi" ] || bds_pair_ready=0
  [ -s "$EFISP_DIR/boot.efi.gm2p" ] || bds_pair_ready=0
  [ -s "$EFISP_DIR/boot.efi.tzmap" ] || bds_pair_ready=0
  mode2_profile_installed || bds_pair_ready=0
  abl_tzmap_installed || bds_pair_ready=0
  if [ "$bds_pair_ready" != "1" ]; then
    if efisp_has_mz && gbl_exploit_present; then
      write_state error "$TEXT_BDS_OLD_VER"
    else
      write_state error "$TEXT_BDS_NOT_INSTALLED"
    fi
    return 2
  fi
  current_slot=$(detect_current_slot) || {
    write_log "$TEXT_NO_SLOT"
    return 1
  }
  if ! install_efisp_pair "$EFISP_DIR" "$EFISP_DIR/boot.efi" \
       "$EFISP_DIR/boot.efi.gm2p" "$EFISP_DIR/boot.efi.tzmap" \
       "$current_slot"; then
    write_log "$TEXT_EFISP_FLASH_FAILED"
    return 1
  fi
  write_log "$TEXT_EFISP_FLASH_OK"
  write_log "$TEXT_EFISP_FILES_OK"
  return 0
}
print_status() {
  ensure_runtime
  current_slot=$(detect_current_slot)
  target_slot=$(other_slot "$current_slot")
  _state=$(read_line "$STATE_FILE")
  running=0
  pid=$(current_pid)
  [ -n "$pid" ] || pid=$(lock_owner_pid)
  [ -n "$pid" ] && running=1
  _msg=$(read_line "$MESSAGE_FILE")
  _upd=$(read_line "$UPDATED_FILE")
  _task=$(read_line "$TASK_FILE")
  config_read_error=0
  status_mode=
  status_defaulted=
  entry_id=$(config_active_id "$current_slot")
  if config_mode_for_slot "$EFISP_DIR/canoe.cfg" "$current_slot"; then
    status_mode="$PREFERRED_MODE"
    status_defaulted="$MODE_DEFAULTED"
  else
    config_read_error=1
  fi

  out="CURRENT_SLOT=$current_slot|TARGET_SLOT=$target_slot|RUNNING=$running|PID=$pid|STATE=$_state|MESSAGE=$_msg|UPDATED_AT=$_upd|TASK_ID=$_task|ENTRY_ID=$entry_id|ENTRY_MODE=$status_mode|ENTRY_MODE_DEFAULTED=$status_defaulted|CONFIG_READ_ERROR=$config_read_error|USER_LANG=$LANG"
  emit "$out"
}

# Return values: 0=success, 1=patch failure, 2=no patch selected.
exec_patch_by_args() {
  arg_str="$1"
  slot_override="$2"

  arg_vendor_boot=0
  arg_debug=0
  case ",$arg_str," in *,vendor_boot=1,*) arg_vendor_boot=1 ;; esac
  case ",$arg_str," in *,debug=1,*) arg_debug=1 ;; esac

  if [ "$arg_vendor_boot" != "1" ]; then
    write_log "$TEXT_PATCH_NO_SELECTED"
    return 2
  fi

  if [ -n "$slot_override" ]; then
    target_slot_suffix="$slot_override"
  else
    target_slot_suffix=$(detect_current_slot)
    [ -z "$target_slot_suffix" ] && { write_log "$TEXT_NO_SLOT"; return 1; }
  fi
  slot_letter=$(slot_suffix_to_letter "$target_slot_suffix")

  _old_pwd="$PWD"
  cd "$BINDIR" || { write_log "$TEXT_BIN_NOT_FOUND: $BINDIR"; return 1; }
  write_log "$TEXT_PATCH_VENDORBOOT_START"
  if [ "$arg_debug" = "1" ]; then
    write_log "$TEXT_PATCH_DEBUG_SAVE"
  elif [ ! -x "$BINDIR/canoe_vendor_boot.sh" ]; then
    write_log "$TEXT_BIN_NOT_FOUND: canoe_vendor_boot.sh"
    cd "$_old_pwd"
    return 1
  elif ! sh "$BINDIR/canoe_vendor_boot.sh" "$slot_letter" >> "$LOG_FILE" 2>&1; then
    write_log "$TEXT_PATCH_ERR"
    cd "$_old_pwd"
    return 1
  fi
  write_log "$TEXT_PATCH_VENDORBOOT_DONE"
  cd "$_old_pwd"
  return 0
}

run_flash() {
  mode_full="$1"
  worker_task_id="$2"
  debug=no

  # ========== 修复1：改用 shell 参数扩展解析，兼容所有 Android 环境 ==========
  case "$mode_full" in
    *,*)
      base_mode="${mode_full%%,*}"
      patch_args="${mode_full#*,}"
      ;;
    *)
      base_mode="$mode_full"
      patch_args=""
      ;;
  esac

  if [ "$base_mode" = "debug" ]; then
    debug=yes
    base_mode=update-efisp
  fi

  ensure_runtime
  if [ -z "$worker_task_id" ]; then
    worker_task_id="direct-$(date +%s)-$$"
    acquire_task_lock "$worker_task_id" || exit 1
    echo "$worker_task_id" > "$TASK_FILE"
    : > "$LOG_FILE"
  fi
  claim_worker_lock "$worker_task_id" || exit 1

  case "$base_mode" in
    update-efisp|update-bds-tools|skip-efisp) ;;
    *) write_state error "invalid flash action"; exit 1 ;;
  esac

  if [ "$base_mode" = "update-bds-tools" ]; then
    write_state running "$TEXT_UPDATING_BDS_TOOLS"
    update_bds_tools
    res=$?
    if [ $res -eq 0 ]; then
      write_state success "$TEXT_BDS_TOOLS_OK"
    elif [ $res -eq 2 ]; then
      :
    else
      write_state error "$TEXT_BDS_TOOLS_FAIL"
    fi
    exit 0
  fi

  current_slot=$(detect_current_slot)
  target_slot=$(other_slot "$current_slot")
  [ -z "$current_slot" ] && { write_state error "$TEXT_NO_SLOT"; exit 1; }
  [ -z "$target_slot" ] && { write_state error "$TEXT_NO_TARGET_SLOT"; exit 1; }
  arg_abl_supplied=0
  arg_vbmeta_supplied=0
  case ",$patch_args," in *,abl=supplied,*) arg_abl_supplied=1 ;; esac
  case ",$patch_args," in *,vbmeta=supplied,*) arg_vbmeta_supplied=1 ;; esac

  # skip-efisp 模式：仅修补分区，不碰 ABL/efisp
  if [ "$base_mode" = "skip-efisp" ]; then
    write_state running "$TEXT_PATCH_ONLY"
    write_log "$TEXT_PATCH_ONLY"

    if [ -z "$patch_args" ]; then
    write_log "$TEXT_PATCH_NO_SELECTED"
      write_state error "$TEXT_PATCH_NO_SELECTED"
      exit 0
    fi

    exec_patch_by_args "$patch_args" "$target_slot"
    res=$?
    if [ $res -eq 0 ]; then
      write_state success "$TEXT_ALL_OK_NO_EFISP"
    elif [ $res -eq 2 ]; then
    write_log "$TEXT_PATCH_NO_SELECTED"
      write_state error "$TEXT_PATCH_NO_SELECTED"
    else
      write_state error "$TEXT_PATCH_ERR"
    fi
    exit 0
  fi

  # update-efisp 模式：刷 ABL + 更新 efisp + 可选修补
  write_state running "$TEXT_FLASHING $target_slot"
  abl=$(partition_path abl "$target_slot")
  vbmeta=$(partition_path vbmeta "$target_slot")
  current_abl=$(partition_path abl "$current_slot")
  current_vbmeta=$(partition_path vbmeta "$current_slot")
  abl_source_kind=partition
  vbmeta_source_kind=partition
  derivation_abl="$abl"
  derivation_vbmeta="$vbmeta"
  derivation_source_abl="$current_abl"
  derivation_source_vbmeta="$current_vbmeta"
  if [ "$arg_abl_supplied" = "1" ]; then
    if [ ! -s "$SUPPLIED_DIR/abl.img" ]; then
      write_log "supplied abl.img is missing or empty"
      write_state error "supplied abl.img is missing or empty"
      exit 1
    fi
    abl_source_kind=supplied
    derivation_abl="$SUPPLIED_DIR/abl.img"
    derivation_source_abl="$SUPPLIED_DIR/abl.img"
  fi
  if [ "$arg_vbmeta_supplied" = "1" ]; then
    if [ ! -s "$SUPPLIED_DIR/vbmeta.img" ]; then
      write_log "supplied vbmeta.img is missing or empty"
      write_state error "supplied vbmeta.img is missing or empty"
      exit 1
    fi
    vbmeta_source_kind=supplied
    derivation_vbmeta="$SUPPLIED_DIR/vbmeta.img"
    derivation_source_vbmeta="$SUPPLIED_DIR/vbmeta.img"
  fi
  write_log "canoe: abl source=$abl_source_kind vbmeta source=$vbmeta_source_kind"
  next_slot=$(next_boot_slot || :)
  if [ -z "$next_slot" ] || [ "$next_slot" = "$current_slot" ]; then
    write_log "$TEXT_NO_TARGET_SLOT"
    write_state error "$TEXT_NO_TARGET_SLOT"
    exit 1
  fi
  active_slot="$next_slot"

  if [ "$debug" = "yes" ]; then
    update_efisp "$derivation_abl" "$derivation_vbmeta" yes \
      "$derivation_source_abl" "$derivation_source_vbmeta" "$active_slot"
    efisp_res=$?

    patch_res=0
    if [ -n "$patch_args" ]; then
      exec_patch_by_args "debug=1,$patch_args" "$target_slot"
      patch_res=$?
    fi

    if [ $efisp_res -eq 0 ] && [ $patch_res -ne 1 ]; then
      write_state success "$TEXT_DEBUG_DONE $RUNTIME_DIR"
    else
      write_state error "$TEXT_DEBUG_FAILED"
    fi
    exit 0
  fi

  efisp_fail=0
  skip_abl_flash=0
  update_efisp "$derivation_abl" "$derivation_vbmeta" no \
    "$derivation_source_abl" "$derivation_source_vbmeta" "$active_slot"
  res=$?
  if [ $res -eq 3 ]; then
    write_log "ABL/vbmeta/config transaction failed"
    write_state error "ABL/vbmeta/config transaction failed"
    exit 3
  elif [ $res -eq 1 ]; then
    efisp_fail=1
    write_state running "$TEXT_EFISP_WARN"
    if [ "$gbl_vuln" = "1" ]; then
      # The target is the retained image when its GBL is exploitable. Do not
      # flash the current slot after an efisp failure and create a mixed pair.
      skip_abl_flash=1
      write_log "$TEXT_GBL_VULN_SKIP"
    fi
  elif [ $res -eq 2 ]; then
    # ========== 修复2：有漏洞仅跳过ABL刷写，继续执行修补 ==========
    skip_abl_flash=1
    write_log "$TEXT_GBL_VULN_SKIP"
  fi


  # 刷写 ABL 到目标槽位（无漏洞时执行）
  if [ "$skip_abl_flash" != "1" ]; then
    for part in $IMAGE_NAMES; do
      dst=$(partition_path "$part" "$target_slot")
      src=$(partition_path "$part" "$current_slot")
      blockdev --setrw "$dst" >> "$LOG_FILE" 2>&1 || { write_state error "$TEXT_SET_RW_FAILED"; exit 1; }
      dd if="$src" of="$dst" bs=4M conv=fsync >> "$LOG_FILE" 2>&1 || { write_state error "$TEXT_FLASH_PART failed"; exit 1; }
      if ! sync; then
        write_log "$TEXT_FLASH_PART $part sync failed"
        write_state error "$TEXT_FLASH_PART $part sync failed"
        exit 1
      fi
      write_log "$TEXT_FLASH_PART $part -> $dst $TEXT_FLASH_OK"
    done

  fi


  # 执行目标槽位修补
  patch_fail=0
  if [ -n "$patch_args" ]; then
    write_log "$TEXT_PATCH_START (target slot)"
    exec_patch_by_args "$patch_args" "$target_slot"
    if [ $? -ne 0 ]; then
      patch_fail=1
      write_log "$TEXT_PATCH_ERR on target slot"
    fi
  fi

  # 最终状态判定
  if [ $efisp_fail -eq 1 ] || [ $patch_fail -eq 1 ]; then
    write_log "BL done, partial failed"
    write_state warning "BL done, partial failed"
  elif [ "$skip_abl_flash" = "1" ] && [ -n "$patch_args" ]; then
    write_log "$TEXT_PATCH_DONE"
    write_state success "$TEXT_PATCH_DONE"
  elif [ "$skip_abl_flash" = "1" ]; then
    write_log "$TEXT_GBL_VULN_SKIP"
    write_state success "$TEXT_GBL_VULN_SKIP"
  else

    write_log "$TEXT_ALL_OK"
    write_state success "$TEXT_ALL_OK"
  fi
}
mode2_profile_installed() {
  [ -s "$EFISP_DIR/boot.efi.gm2p" ] || return 1
  "$BINDIR/mode2_profile" validate --input "$EFISP_DIR/boot.efi.gm2p" >> "$LOG_FILE" 2>&1
}
abl_tzmap_installed() {
  [ -s "$EFISP_DIR/boot.efi.tzmap" ] || return 1
  "$BINDIR/abl_tzmap" validate "$EFISP_DIR/boot.efi.tzmap" >> "$LOG_FILE" 2>&1
}

mode_request_preflight() {
  mode_value="$1"
  MODE_PREFLIGHT_ERROR=ENTRY_MODE_PREFLIGHT
  case "$mode_value" in 0|1|2) ;; *) return 1 ;; esac
  [ -d "$EFISP_DIR" ] || return 1
  if [ "$mode_value" = "2" ]; then
    [ -s "$EFISP_DIR/boot.efi.gm2p" ] || {
      MODE_PREFLIGHT_ERROR=MODE2_PROFILE_MISSING
      return 1
    }
    "$BINDIR/mode2_profile" validate --input "$EFISP_DIR/boot.efi.gm2p" >> "$LOG_FILE" 2>&1 || {
      MODE_PREFLIGHT_ERROR=MODE2_PROFILE_INVALID
      return 1
    }
    [ -x "$BINDIR/abl_tzmap" ] || {
      MODE_PREFLIGHT_ERROR=TZMAP_TOOL_MISSING
      return 1
    }
    abl_tzmap_installed || {
      MODE_PREFLIGHT_ERROR=TZMAP_INVALID
      return 1
    }
  fi
  return 0
}

run_config_mode_worker() {
  mode_value="$1"
  worker_task_id="$2"
  claim_worker_lock "$worker_task_id" || exit 1
  if ! mode_request_preflight "$mode_value"; then
    write_state error "entry mode preflight failed"
    exit 1
  fi
  current_slot=$(detect_current_slot) || {
    write_state error "$TEXT_NO_SLOT"
    exit 1
  }
  entry_id=$(config_active_id "$current_slot")
  [ -x "$CANOE_BOOTMGR" ] || {
    write_state error "$TEXT_BIN_NOT_FOUND: $CANOE_BOOTMGR"
    exit 1
  }
  if ! "$CANOE_BOOTMGR" --json --boot-root "$EFISP_DIR" entry mode \
       --id "$entry_id" --mode "$mode_value" >> "$LOG_FILE" 2>&1; then
    write_state error "canoe.cfg mode write failed"
    exit 1
  fi
  write_state success "entry mode saved"
  exit 0
}


start_mode() {
  mode_value="$1"
  ensure_runtime
  case "$mode_value" in
    0|1|2) ;;
    *) emit "STARTED=0|ERROR=invalid mode"; return 1 ;;
  esac
  task_id="$(date +%s)-$$"
  if ! acquire_task_lock "$task_id"; then
    emit "ALREADY_RUNNING=1"
    return 0
  fi
  if ! mode_request_preflight "$mode_value"; then
    echo "$task_id" > "$TASK_FILE"
    write_state error "entry mode preflight failed: $MODE_PREFLIGHT_ERROR"
    cleanup_lock
    emit "STARTED=0|TASK_ID=$task_id|ERROR_CODE=$MODE_PREFLIGHT_ERROR|ERROR=entry mode preflight failed"
    return 1
  fi
  : > "$LOG_FILE"
  echo "$task_id" > "$TASK_FILE"
  write_state running "saving entry mode"
  setsid sh "$0" config-mode-worker "$mode_value" "$task_id" >/dev/null 2>&1 </dev/null &
  sleep 1
  if [ -n "$(current_pid)" ]; then
    emit "STARTED=1|TASK_ID=$task_id"
  else
    st=$(read_line "$STATE_FILE")
    if [ "$st" = "running" ]; then
      write_state error "entry mode worker failed to start"
      st=error
    fi
    cleanup_lock
    emit "FINISHED=$st|TASK_ID=$task_id"
  fi
}

run_patch() {
  arg_str="$1"
  worker_task_id="$2"

  ensure_runtime
  if [ -z "$worker_task_id" ]; then
    worker_task_id="direct-$(date +%s)-$$"
    acquire_task_lock "$worker_task_id" || exit 1
    echo "$worker_task_id" > "$TASK_FILE"
    : > "$LOG_FILE"
  fi
  claim_worker_lock "$worker_task_id" || exit 1

  write_state running "$TEXT_PATCH_START"
  write_log "$TEXT_PATCH_START"
  write_log "$TEXT_PATCH_ARGS: $arg_str"

  exec_patch_by_args "$arg_str"
  res=$?

  if [ $res -eq 1 ]; then
    write_log "$TEXT_PATCH_BOTH_ERR"
    write_state error "$TEXT_PATCH_BOTH_ERR"
    exit 1
  elif [ $res -eq 2 ]; then
    write_log "$TEXT_PATCH_NO_SELECTED"
    write_state error "$TEXT_PATCH_NO_SELECTED"
    exit 0
  fi

    write_log "$TEXT_PATCH_DONE"
  write_state success "$TEXT_PATCH_DONE"
  exit 0
}

start_patch() {
  ensure_runtime
  task_id="$(date +%s)-$$"
  if ! acquire_task_lock "$task_id"; then
    emit "ALREADY_RUNNING=1"
    return 0
  fi
  : > "$LOG_FILE"
  echo "$task_id" > "$TASK_FILE"
  write_state running "$TEXT_PATCH_START"
  setsid sh "$0" patch "$1" "$task_id" >/dev/null 2>&1 </dev/null &
  sleep 1
  if [ -n "$(current_pid)" ]; then
    emit "STARTED=1|TASK_ID=$task_id"
  else
    st=$(read_line "$STATE_FILE")
    if [ "$st" = "running" ]; then
      write_state error "patch worker failed to start"
      st=error
    fi
    cleanup_lock
    emit "FINISHED=$st|TASK_ID=$task_id"
  fi
}

start_flash() {
  case "$1" in
    update-efisp|update-efisp,*|update-bds-tools|skip-efisp|skip-efisp,*|debug|debug,*) ;;
    *) emit "STARTED=0|ERROR=invalid action"; return 1 ;;
  esac

  ensure_runtime
  task_id="$(date +%s)-$$"
  if ! acquire_task_lock "$task_id"; then
    emit "ALREADY_RUNNING=1"
    return 0
  fi
  : > "$LOG_FILE"
  echo "$task_id" > "$TASK_FILE"
  case "$1" in
    update-bds-tools*) _start_msg="$TEXT_UPDATING_BDS_TOOLS" ;;
    skip-efisp*) _start_msg="$TEXT_PATCH_ONLY" ;;
    debug*) _start_msg="$TEXT_DEBUG_MODE" ;;
    *) _start_msg="$TEXT_FLASHING" ;;
  esac
  write_state running "$_start_msg"
  setsid sh "$0" flash "$1" "$task_id" >/dev/null 2>&1 </dev/null &
  sleep 1
  if [ -n "$(current_pid)" ]; then
    emit "STARTED=1|TASK_ID=$task_id"
  else
    st=$(read_line "$STATE_FILE")
    if [ "$st" = "running" ]; then
      write_state error "flash worker failed to start"
      st=error
    fi
    cleanup_lock
    emit "FINISHED=$st|TASK_ID=$task_id"
  fi
}

print_log() { cat "$LOG_FILE"; }
tail_log() { tail -n200 "$LOG_FILE" | awk '{printf "%s@NL@", $0}'; }

clear_log() {
  ensure_runtime
  task_busy && { emit "BUSY=1"; return; }
  : > "$LOG_FILE"
  write_state idle "$TEXT_LOG_CLEARED"
  emit "CLEARED=1"
}

case "$1" in
  status) print_status ;;
  flash) run_flash "$2" "$3" ;;
  start) start_flash "$2" ;;
  patch) run_patch "$2" "$3" ;;
  start-patch) start_patch "$2" ;;
  start-mode) start_mode "$2" ;;
  config-mode-worker) run_config_mode_worker "$2" "$3" ;;
  log) print_log ;;
  tail) tail_log ;;
  clear-log) clear_log ;;
  *) exit 1 ;;
esac
