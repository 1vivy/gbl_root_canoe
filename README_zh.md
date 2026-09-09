# GBL Root Canoe

[English](README.md)

Canoe 为受支持的高通设备提供受管理的启动环境。Mode 1/2 可以向 Android
呈现锁定状态，但不会重新锁定实际 Bootloader。

启动链为：签名且存在漏洞的 ABL → 原始 efisp 中的 BDS.efi → persist/efisp.fat → 选中的加载器。
启动根目录是 ext4 persist 内的 32 MiB FAT16 容器。普通菜单选择仅影响本次启动；
显式 Save as default 才保存偏好。

7.0.0-b4 的发布界面是桌面与 KernelSU WebUI 共用的一个应用；原生工作进程也供 KSU 安装器使用，负责审阅、
写入校验、收据与恢复。独立命令分别管理已挂载目录、准备镜像、创建或删除容器。
常规 FAT 操作使用系统原生文件系统，离线 ext4 操作使用未修改的 libext2fs。
旧的 ext4 efisp 目录保留但不导入，升级用户需要重新创建启动项。

参见[安装](wiki/docs/zh/install.md)、[命令](wiki/docs/commands.md)、
[旧版重装](wiki/docs/reinstall.md)、[格式化数据矩阵](wiki/docs/format-data.md)、
[构建](wiki/docs/build.md)与[卸载](wiki/docs/zh/uninstall.md)。

项目许可证为 GPL-2.0-or-later。历史资料见 [ARCHIVE.md](ARCHIVE.md)。
