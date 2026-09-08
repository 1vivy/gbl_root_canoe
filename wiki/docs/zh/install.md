# 安装 Canoe

桌面与 KSU 安装器共用原生部署流程。首次模块安装可用音量键选择仅安装管理器、立即部署或取消；更新不会自动部署。释放按键后再选择下一项，超时保留仅安装管理器。

桌面首次部署先记录旧 EFISP 模组使用历史，再在 Android Fastbootd 中审阅活动槽 ABL 和原始 efisp 写入及恢复镜像。重启后通常自动进入 Super Fastboot，校验完成后继续准备。A/B/Both 是手动选择，各槽独立准备。WebUI 完整安装保留 Android 原生文件选择器。

启动根目录改为 persist/efisp.fat；旧 efisp 目录保留但不迁移。gbl-chainload 与 Canoe 6.3.5 及更早版本用户需要重新安装并重建启动项。

参见[完整说明](../install.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
