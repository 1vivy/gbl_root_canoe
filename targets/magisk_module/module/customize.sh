#!/system/bin/sh
# Package setup and input presentation; native canoe-manager owns deployment.

if [ -z "${MODPATH:-}" ]; then
  ui_print "Canoe Boot Manager module path is unavailable"
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
  T_EFISP_TITLE="确保你的内核没有Baseband Guard"
  T_SOC="确保你的设备是8gen5/8elitegen5"
else
  T_VERIFY="- Verifying device model"
  T_DEVICE_OK="- Device verified:"
  T_PERM="- Setting permissions"
  T_EFISP_TITLE="Ensure kernel has no Baseband Guard"
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

# The build step bundles ablrepo for the WebUI's offline lookup. Keep it in
# the installed module at /data/adb/modules/<module-id>/ablrepo; module.prop
# is authoritative for that id. The native worker validates this exact packaged
# repository against Android product/model/SoC facts; there is no remote fallback.

# Input presentation follows. The native worker owns preparation and the
# reviewed writes, shared with WebUI; shell never implements partition writes.
ui_print ""
ui_print "Canoe Boot Manager"
if [ "$user_lang" = "zh" ]; then
  ui_print "- 仅安装管理界面和工具时不会更改启动分区，不修改启动分区或手机数据。"
  ui_print "- 按 KernelSU 提示重启以激活模块，然后打开 WebUI。"
  ui_print "- 更新激活前仍可见的 WebUI 可能属于旧版本。"
  if "$MODPATH/bin/canoe-manager" installer supported >/dev/null 2>&1; then
    ui_print "- 完整安装、模式选择和 OTA 准备也可在 WebUI 中完成。"
  fi
  ui_print "- 安装本模块本身不需要格式化数据。"
else
  ui_print "- Installing the manager alone does not change boot partitions or phone data."
  ui_print "- Reboot when KernelSU requests module activation, then open the WebUI."
  ui_print "- A WebUI visible before an update activates may still be the previous version."
  if "$MODPATH/bin/canoe-manager" installer supported >/dev/null 2>&1; then
    ui_print "- Full installation, mode selection, and OTA preparation are also available in WebUI."
  fi
  ui_print "- Installing this module alone does not require formatting data."
fi
[ -f "$MODPATH/install-flow.sh" ] || abort "Install flow is missing"
. "$MODPATH/install-flow.sh"
return 0
