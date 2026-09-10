# 使用 Canoe

General/Overview 显示设备状态和已保存操作；Deploy 用于准备、审阅和写入；Boot entries 管理 EFI/BLS；Settings 提供策略、语言和卸载；Diagnostics 提供观察信息及记录。

Prepare Mode 1 boot images 直接进入镜像准备，默认使用活动槽验证资料并保留已安装模式，不更新加载器、BDS、工具或启动项模式。未完成操作可重试或审阅 Revert，成功收据仍可查看。

普通 BDS 菜单选择只影响本次启动。显式 Save as default 才保存偏好，并保留已验证的 canoe.cfg.prev 作为缺失/损坏配置的回退。

参见[完整说明](../usage.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。

启动时音量上直接进入 Super Fastboot，音量下打开启动菜单。空启动根目录保留两秒超时到 Super Fastboot，不再额外等待普通按键窗口。Advanced 提供独立的默认项、Android 项模式及启动策略设置；Reboot 提供 Fastbootd、Bootloader、Recovery、System。隐藏 Booting… 与 CBM 共享 `show-booting` 配置。
