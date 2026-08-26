# USB Mass Storage 指南

Superfastboot 可以将一个物理分区导出给连接的电脑，作为普通 USB 磁盘使用。ADB 不可用时，这是一条重要的修复路径：`persist` 中包含启动根目录，无法进入 Android 的设备则可以从 `logfs` 取出启动日志。

## 进入导出功能

在 BDS 启动菜单中选择 **USB Mass Storage**，再选择一个出现的目标并确认。只有在 `logfs` 分区存在时才会显示 `logfs`。

也可以在 fastboot 中使用：

```bash
fastboot oem mass-storage             # persist（默认）
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

在选择目标之前或之后将设备连接到电脑，然后等待电脑识别导出的磁盘。

## 导出目标

- **`persist`**——启动根目录位于 `/efisp` 下，其中包括 `canoe.cfg`、已配置的启动项及其附属文件。设备没有可用 ADB 时，它是修复通道。
- **`logfs`**——只有分区存在时才提供；用于从无法启动的设备中取出启动日志。

导出 `persist` 前 BDS 会显示警告，因为这是正在使用中的文件系统。请把导出的磁盘当作设备当前正在使用的存储，只进行明确的修复操作，并在结束会话前完成写入。

## 会话限制与结束方式

每次会话只导出一个分区，作为一个 USB LUN 使用；不会同时暴露 `persist` 和 `logfs`。在设备上按**音量下**（Volume Down）停止导出；拔线或 USB 链路中断也会结束主机会话。会话结束后，BDS 会返回菜单。

## Windows 挂载与修复

Windows 压缩包内附带 `platform-tools`。ext4 读写路径使用 **WinFsp 与 LKL `lklfuse`**。这些组件不会被 vendored 进仓库；首次使用时才下载并进行 SHA-256 校验，然后交给 Windows 辅助工具使用。

在 Windows 上修复启动根目录：

1. 开始导出 `persist`，并确认正在使用中的文件系统警告。
2. 等待 Windows 识别导出的 USB 磁盘。
3. 使用已下载并验证的 WinFsp + LKL `lklfuse` 桥接路径，以读写方式挂载该磁盘的 ext4 文件系统。
4. 编辑 `persist/efisp` 下的启动根目录（例如修复 `canoe.cfg` 或启动项文件），完成写入后刷新并安全卸载。
5. 在设备上按**音量下**结束导出会话。

每次会话只能导出一个分区。配置语法请参阅规范版 [`canoe.cfg` 契约](../canoe-cfg.md)。
