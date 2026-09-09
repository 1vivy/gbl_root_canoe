#!/system/bin/sh
# Presentation only. The staged native backend owns all preparation and writes.
# One released key press makes one choice. A held key cannot confirm two pages.
canoe_key() {
  canoe_down=
  canoe_deadline=$(( $(date +%s) + 90 ))
  while [ "$(date +%s)" -lt "$canoe_deadline" ]; do
    if ! read -r -t 2 canoe_event <&3; then
      kill -0 "$canoe_input_pid" 2>/dev/null || return 1
      continue
    fi
    case "$canoe_event" in
      *KEY_VOLUMEUP*DOWN*) canoe_down=up ;;
      *KEY_VOLUMEDOWN*DOWN*) canoe_down=down ;;
      *KEY_VOLUMEUP*UP*) [ "$canoe_down" = up ] && { echo up; return 0; } ;;
      *KEY_VOLUMEDOWN*UP*) [ "$canoe_down" = down ] && { echo down; return 0; } ;;
    esac
  done
  return 1
}
canoe_close_input() {
  kill "$canoe_input_pid" 2>/dev/null || :
  wait "$canoe_input_pid" 2>/dev/null || :
  exec 3<&-
  rm -f "$canoe_fifo"
}
canoe_choose() {
  canoe_fifo=$TMPDIR/canoe-keys-$$
  rm -f "$canoe_fifo"
  mkfifo "$canoe_fifo" || { canoe_choice=timeout; return; }
  /system/bin/getevent -ql > "$canoe_fifo" 2>/dev/null &
  canoe_input_pid=$!
  exec 3< "$canoe_fifo"
  canoe_index=1
  ui_print "$1"
  shift
  ui_print "Vol+ moves; Vol- selects. Release the key between presses."
  while :; do
    eval 'canoe_label=${'"$canoe_index"'}'
    ui_print "> $canoe_label"
    canoe_pressed=$(canoe_key) || { canoe_choice=timeout; canoe_close_input; return; }
    case "$canoe_pressed" in
      up) canoe_index=$(( canoe_index + 1 )); [ "$canoe_index" -le "$#" ] || canoe_index=1 ;;
      down) canoe_choice=$canoe_index; canoe_close_input; return ;;
    esac
  done
}
canoe_install_flow() {
  # Check the staged worker for both first installs and manager updates.
  if ! "$MODPATH/bin/canoe-manager" installer supported >/dev/null 2>&1; then
    if [ "${user_lang:-en}" = zh ]; then
      ui_print "- 此 b5 版本仅安装管理器，CANOE-BDS 部署功能尚未就绪。"
    else
      ui_print "- This b5 build installs the manager only. CANOE-BDS deployment is not available yet."
    fi
    return
  fi
  # Updates activate the new manager normally without offering partition writes.
  if [ -x /data/adb/modules/fake_bl_efisp/bin/canoe-manager ]; then
    ui_print "- Manager update only. Service CANOE-BDS through WebUI after activation."
    return
  fi
  canoe_choose "CANOE-BDS first setup" "Install manager only" "Deploy CANOE-BDS now" "Cancel module installation"
  case "$canoe_choice" in
    1|timeout) return ;;
    3) abort "Module installation cancelled" ;;
  esac
  canoe_choose "Deployment mode" "Mode 2" "Mode 1" "Install manager only"
  case "$canoe_choice" in 1) canoe_mode=2 ;; 2) canoe_mode=1 ;; *) return ;; esac
  canoe_choose "Recovery image on the active slot" "Standard recovery" "Custom recovery image" "Install manager only"
  canoe_custom=
  case "$canoe_choice" in 2) canoe_custom=--custom-recovery ;; 1) : ;; *) return ;; esac
  canoe_patch=
  if [ "$canoe_mode" = 1 ]; then
    canoe_choose "vendor_boot preparation" "Keep current vendor_boot" "Patch vendor_boot" "Install manager only"
    case "$canoe_choice" in 2) canoe_patch=--patch-vendor-boot ;; 1) : ;; *) return ;; esac
  fi
  canoe_choose "History of the current phone data" "I am not sure" "Fully unlocked; current data has never used an efisp mode" "Current data has used CANOE-BDS or another efisp mode" "Install manager only"
  canoe_history=
  case "$canoe_choice" in 2) canoe_history=--first-unlocked-install ;; 3) canoe_history=--previous-efisp ;; 1) : ;; *) return ;; esac
  canoe_state_base=/data/adb/canoe-manager/b5/installer-$(date +%s)-$$
  canoe_review=$TMPDIR/canoe-review.txt
  canoe_cleaned=no
  for canoe_phase in 1 2; do
    canoe_state=$canoe_state_base-$canoe_phase
    if ! "$MODPATH/bin/canoe-manager" bootstrap --module-root "$MODPATH" --state-dir "$canoe_state" --mode "$canoe_mode" $canoe_custom $canoe_patch $canoe_history > "$canoe_review" 2>&1; then
      cat "$canoe_review"
      if [ "$canoe_cleaned" = yes ]; then
        ui_print "- CANOE-BDS installation was not started. The reviewed old mod files remain removed."
      else
        ui_print "- Deployment was not started. No CANOE-BDS partitions were written."
      fi
      canoe_choose "Continue setup" "Install manager only; complete Full installation in WebUI after reboot" "Cancel module installation"
      [ "$canoe_choice" != 2 ] || abort "Module installation cancelled"
      return
    fi
    cat "$canoe_review" | sed '/^CONFIRM=/d; /^STAGE=/d'
    canoe_token=$(sed -n 's/^CONFIRM=//p' "$canoe_review")
    canoe_stage=$(sed -n 's/^STAGE=//p' "$canoe_review")
    [ "${#canoe_token}" -eq 64 ] || abort "Native install review is incomplete"
    case "$canoe_stage" in
      cleanup)
        if [ "$canoe_cleaned" = yes ]; then
          ui_print "- More old mod files appeared. Install the manager only and review these files in WebUI."
          return
        fi
        canoe_apply_label="Remove old mod files and continue"
        ;;
      deploy) canoe_apply_label="Apply this deployment" ;;
      *) abort "Native installer stage is unavailable" ;;
    esac
    canoe_choose "Review the changes above" "Install manager only" "$canoe_apply_label" "Cancel module installation"
    case "$canoe_choice" in 2) : ;; 3) abort "Module installation cancelled" ;; *) return ;; esac
    if ! "$MODPATH/bin/canoe-manager" bootstrap --module-root "$MODPATH" --state-dir "$canoe_state" --confirm "$canoe_token"; then
      ui_print "- The changes did not complete. Install the manager only, then open WebUI to review the failure or remove CANOE-BDS."
      ui_print "- Recovery records: /data/adb/canoe-manager/b5"
      abort "CANOE-BDS setup stopped; completed writes remain recorded"
    fi
    [ "$canoe_stage" = cleanup ] || return 0
    canoe_cleaned=yes
    ui_print "- Old mod files were removed. Preparing a new CANOE-BDS installation review."
  done
}
canoe_install_flow
