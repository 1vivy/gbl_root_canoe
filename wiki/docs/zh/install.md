# 安装 Canoe

KSU 模块安装和更新仅安装管理器及工具，不部署 CANOE-BDS、不修改启动分区、不迁移旧模组，也不格式化数据。按 KernelSU 提示正常重启激活后，打开 WebUI 使用完整 Deploy 和原生文件选择器；需要时重新提供固件镜像。安装时没有音量键部署流程。完全解锁设备的首次部署若需要格式化数据，建议使用主机网页，它不会随手机数据格式化而消失。

在线版从 Overview 选择 **Fresh install / redeploy**；首次安装与从旧 EFISP 模组重装都从这里进入，后续镜像、清理、备份、槽位和数据格式化判断由应用按当前设备分流，不再按旧场景另选教程。先记录旧 EFISP 模组使用历史，再在 Android Fastbootd 中审阅活动槽 ABL 和原始 efisp 写入及恢复镜像。重启后通常自动进入 Super Fastboot，校验完成后继续准备。A/B/Both 是手动选择，各槽独立准备。WebUI 完整安装保留 Android 原生文件选择器。在线版需使用支持 WebUSB 的 Chromium 浏览器，Linux 需一次性配置 [USB 访问规则](./linux-usb.md)；首次安装要求先独立保存 persist 备份。

启动根目录改为 persist/efisp.fat；旧 efisp 目录不迁移。从 gbl-chainload、Canoe 6.3.5 或其他 EFISP 模组重装时仍选择 **Fresh install / redeploy** 并如实选择已有修改；应用会重建 Android 启动项，只有用户另加的 EFI/BLS 项需要手动重建。

参见[完整说明](../install.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
