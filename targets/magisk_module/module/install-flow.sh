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
  # Updates activate the new manager normally; servicing stays an explicit WebUI operation.
  if [ -x /data/adb/modules/fake_bl_efisp/bin/canoe-bootmgr ]; then
    ui_print "- Manager update only. Service Canoe through WebUI after activation."
    return
  fi
  canoe_choose "Canoe first setup" "Install manager only" "Deploy Canoe now" "Cancel module installation"
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
  mkdir -p /data/adb/canoe || abort "Cannot create recovery directory"
  chmod 0700 /data/adb/canoe
  canoe_state=/data/adb/canoe/install-$(date +%s)-$$
  canoe_review=$TMPDIR/canoe-review.txt
  if ! "$MODPATH/bin/canoe-bootmgr" --boot-root /mnt/vendor/persist/efisp bootstrap --module-root "$MODPATH" --state-dir "$canoe_state" --mode "$canoe_mode" $canoe_custom $canoe_patch > "$canoe_review" 2>&1; then
    cat "$canoe_review"
    ui_print "- Deployment was not started. No Canoe partitions were written."
    canoe_choose "Continue setup" "Install manager only; complete Full installation in WebUI after reboot" "Cancel module installation"
    [ "$canoe_choice" != 2 ] || abort "Module installation cancelled"
    return
  fi
  cat "$canoe_review" | sed '/^CONFIRM=/d'
  canoe_token=$(sed -n 's/^CONFIRM=//p' "$canoe_review")
  [ "${#canoe_token}" -eq 64 ] || abort "Native install review is incomplete"
  canoe_choose "Review the targets and data assessment above" "Install manager only" "Apply this deployment" "Cancel module installation"
  case "$canoe_choice" in 2) : ;; 3) abort "Module installation cancelled" ;; *) return ;; esac
  if ! "$MODPATH/bin/canoe-bootmgr" --boot-root /mnt/vendor/persist/efisp bootstrap --module-root "$MODPATH" --state-dir "$canoe_state" --confirm "$canoe_token"; then
    ui_print "- Deployment did not complete. Inspect recovery records: $canoe_state"
    abort "Canoe deployment failed; completed writes remain recorded"
  fi
}
canoe_install_flow
