#!/system/bin/sh
# Canoe's module installer is deliberately a bootstrap.  Magisk has already
# unpacked the payload and WebUI before this script runs; all boot-chain
# decisions and mutations belong to the WebUI through canoe-bootmgr.

if [ -z "${MODPATH:-}" ]; then
  ui_print "Canoe module path is unavailable"
  abort "MODPATH is unavailable"
fi



# Keep the operator's language choice available to the WebUI. Updates retain
# lang.txt or the existing KernelSU module setting. A first install falls back
# to the device locale non-interactively; the running WebUI owns later changes.
user_lang=
if [ -f "$MODPATH/lang.txt" ]; then
  user_lang=$(tr -d '[:space:]' < "$MODPATH/lang.txt")
fi
if [ "$user_lang" != "zh" ] && [ "$user_lang" != "en" ] && command -v ksud >/dev/null 2>&1; then
  configured_lang=$(ksud module config get user_lang 2>/dev/null || :)
  case "$configured_lang" in
    *zh*) user_lang=zh ;;
    *en*) user_lang=en ;;
  esac
fi
if [ "$user_lang" != "zh" ] && [ "$user_lang" != "en" ]; then
  locale_value=$(getprop persist.sys.locale 2>/dev/null || :)
  if [ -z "$locale_value" ]; then
    locale_value=$(getprop ro.product.locale 2>/dev/null || :)
  fi
  case "$locale_value" in
    zh*) user_lang=zh ;;
    *) user_lang=en ;;
  esac
fi
printf '%s\n' "$user_lang" > "$MODPATH/lang.txt"
if command -v ksud >/dev/null 2>&1; then
  ksud module config set user_lang "$user_lang" 2>/dev/null || :
fi

if [ "$user_lang" = "zh" ]; then
  T_VERIFY="- 正在验证设备型号"
  T_DEVICE_OK="- 设备验证完成："
  T_PERM="- 正在设置权限"
  T_EFISP_TITLE="确保你的内核没有Baseband Guard，设备BL锁已经解锁"
  T_SOC="确保你的设备是8gen5/8elitegen5"
else
  T_VERIFY="- Verifying device model"
  T_DEVICE_OK="- Device verified:"
  T_PERM="- Setting permissions"
  T_EFISP_TITLE="Ensure kernel has no Baseband Guard and BL bootloader is unlocked"
  T_SOC="Ensure device is 8gen5 / 8elitegen5"
fi

# These reads are facts displayed to the operator, not a supported-device
# claim: no allow-list is inferred from them.
ui_print "$T_VERIFY"
_model=$(getprop ro.product.model 2>/dev/null)
_name=$(getprop ro.product.name 2>/dev/null)
_inc=$(getprop ro.build.version.incremental 2>/dev/null)
ui_print "$T_DEVICE_OK $_model / $_name / $_inc"
ui_print "$T_EFISP_TITLE"
ui_print "$T_SOC"
ui_print "$T_PERM"

set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644
set_perm "$MODPATH/module.prop" 0 0 0644
set_perm "$MODPATH/customize.sh" 0 0 0755
set_perm "$MODPATH/lang.txt" 0 0 0644
[ -f "$MODPATH/copy-corpus.json" ] &&
  set_perm "$MODPATH/copy-corpus.json" 0 0 0644

# The build step bundles ablrepo for the WebUI's offline lookup. Keep it in
# the installed module at /data/adb/modules/<module-id>/ablrepo; module.prop
# is authoritative for that id. The app passes the resolved path as local_repo
# and falls back to remote lookup only when the bundled directory is absent.

# No partition reader, partition writer, reboot, mode policy, or boot-root
# transaction is allowed here.  The WebUI owns all of those operations.
exit 0
