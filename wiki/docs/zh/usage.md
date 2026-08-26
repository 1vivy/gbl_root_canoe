# Superfastboot 使用指南

## 启动相关

- 将 BDS 临时启动到内存（不会写入闪存）：

  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```

- 开启 OEM 解锁且开机出现小白字时，**必须按音量加（Volume Up）键才能进入 Superfastboot 模式。**

## 首次运行与启动菜单

如果启动根目录中既没有 `canoe.cfg` 也没有 `boot.efi`，BDS 会显示首次运行界面并直接进入 Super Fastboot。此时没有任何可启动内容，只有 fastboot 能够安装内容。

启动菜单包含：

- **Reboot to Recovery**
- **USB Mass Storage**

USB Mass Storage 会将一个分区作为普通 USB 磁盘导出给连接的电脑。`persist` 的 `/efisp` 中包含启动根目录；设备没有可用 ADB 时，它是修复通道。只有在 `logfs` 分区存在时才提供该选项，它适合从无法启动的设备中取出启动日志。导出 `persist` 前会先显示警告，因为这是正在使用中的文件系统。每次会话只能导出一个分区（一个 USB LUN），按**音量下**（Volume Down）结束会话。

也可以在 fastboot 中使用：

```bash
fastboot oem mass-storage             # persist（默认）
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

完整流程与 Windows 挂载步骤见 [USB Mass Storage 指南](../mass-storage.md)。

## 模式选择与锁定状态

Mode 1 或 Mode 2 启动只有在观测到的状态不满足请求模式时才会修复底层 `DeviceInfo`。`canoe.cfg` 中的 `lockstate never` 会拒绝修复；这次启动随后会如实以 Mode 0 继续。Mode 0 是无 hook 的直通模式，既不读取也不写入 `DeviceInfo`。观测到的状态始终会记录在启动日志中。


## BL 相关

- 锁定 BL，**触发数据清除**：

  ```bash
  fastboot flashing lock
  ```

- 解锁 BL，**不触发数据清除**：

  ```bash
  fastboot flashing unlock
  fastboot flashing unlock_critical
  ```

> 注意：如果 TEE 状态不一致，设备会拒绝下发 data key，从而导致数据无法访问。

## 刷写相关

- 刷写分区镜像：

  ```bash
  fastboot flash <partition> <file.img>
  ```

- 擦除指定分区：

  ```bash
  fastboot erase <partition>
  ```

## 重启相关

- 重启至引导加载器，下一次正常启动进入官方 Fastboot：

  ```bash
  fastboot reboot bootloader
  ```

- 重启至恢复模式，下一次正常启动进入 Recovery：

  ```bash
  fastboot reboot recovery
  ```

- 普通重启设备：

  ```bash
  fastboot reboot
  ```
