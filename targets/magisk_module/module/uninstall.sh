#!/system/bin/sh

MODDIR=${0%/*}

rm -rf "$MODDIR/tmp"
ui_print "Removing Canoe Boot Manager. CANOE-BDS, boot images and phone data remain unchanged."
ui_print "仅移除 Canoe Boot Manager；CANOE-BDS、启动镜像和手机数据保持不变。"
