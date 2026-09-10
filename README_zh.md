# GBL Root Canoe

[English](README.md)

CANOE-BDS 为受支持的高通设备提供受管理的启动环境。Mode 1/2 可以向 Android
呈现锁定状态，但不会重新锁定实际 Bootloader。

```text
签名且存在漏洞的 ABL → 原始 efisp 中的 BDS.efi → persist/efisp.fat → 选中的加载器
```

启动根目录是 ext4 persist 内的 FAT16 容器，按可用空间以 8 MiB 为步长分配
（8–256 MiB）。BDS 挂载容器，加载每个槽位的 EFI/GM2P/TZ-map 文件，并发现
EFI/BLS 启动项。普通选择仅影响本次启动；高级菜单分别保存默认项、受管理启动项
的模式或启动策略。

7.0.0-b6 的发布界面是在线 Canoe Boot Manager 与激活后的 KernelSU WebUI。
[在线应用](https://canoe-boot-manager.1vv.ca) 使用 WASM 与受管理 USB；模块内置
WebUI 和 Android 原生工作进程。安装模块只安装管理器。桌面可执行程序和文件系统
辅助进程已退役；`7.0.0-b4-final` 标签保留此前的实现。

- [从主机或 KernelSU 安装](wiki/docs/zh/install.md)
- [从 gbl-chainload 或旧版 CANOE-BDS 重新安装](wiki/docs/reinstall.md)
- [命令](wiki/docs/commands.md)与[配置](wiki/docs/zh/canoe-cfg.md)
- [OTA](wiki/docs/ota.md)与[格式化数据评估](wiki/docs/format-data.md)
- [USB 大容量存储](wiki/docs/mass-storage.md)与[卸载](wiki/docs/zh/uninstall.md)
- [发布流程](wiki/docs/zh/release.md)

旧的 ext4 `efisp/` 目录会被忽略，不会导入。请重新创建启动项；应用可在审阅后
清理旧修改的文件。签名密钥相同不能证明 OEM 身份，也不能证明镜像适用于另一
槽位。每个 beta 的发布说明会注明该构建实际完成的验证。

项目许可证为 GPL-2.0-or-later。历史资料见 [ARCHIVE.md](ARCHIVE.md)；
[b5 重构审计](docs/b5-reconstruction.md)记录了本次 beta 之前的架构调整。
