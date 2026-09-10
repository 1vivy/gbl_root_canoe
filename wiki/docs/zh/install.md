# 安装 Canoe

KSU 模块安装和更新仅安装管理器及工具，不部署 CANOE-BDS、不修改启动分区、不迁移旧模组，也不格式化数据。按 KernelSU 提示正常重启激活后，打开 WebUI 使用完整 Deploy 和原生文件选择器；需要时重新提供固件镜像。安装时没有音量键部署流程。完全解锁设备的首次部署若需要格式化数据，建议使用主机网页，它不会随手机数据格式化而消失。

桌面首次部署先记录旧 EFISP 模组使用历史，再在 Android Fastbootd 中审阅活动槽 ABL 和原始 efisp 写入及恢复镜像。重启后通常自动进入 Super Fastboot，校验完成后继续准备。A/B/Both 是手动选择，各槽独立准备。WebUI 完整安装保留 Android 原生文件选择器。

启动根目录改为 persist/efisp.fat；旧 efisp 目录保留但不迁移。gbl-chainload 与 Canoe 6.3.5 及更早版本用户需要重新安装并重建启动项。

参见[完整说明](../install.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
