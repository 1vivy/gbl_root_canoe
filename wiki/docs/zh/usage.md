# Superfastboot 使用指南


## 启动相关

- 将 BDS 临时启动到内存（不会写入闪存）：

  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```


## BL 相关

- 锁定 BL，**触发数据清除**：

  ```bash
  fastboot flashing lock (触发清除，原因未知)
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


## 注意事项

- ⚠️ **验证状态：** 主机端构建与测试夹具已通过验证。实机验证范围仅包括一加 Ace 6T（CPH2767，`macan`），运行 OxygenOS `CPH2767_16.0.9.401(EX01)`，通过仅驻留内存的 staged-BDS 路径完成；已确认 sidecar 加载、钩子武装、KeyMaster 重写和 SCM 丢弃。其他设备以及永久安装尚未验证。
- 开启 OEM 解锁且开机出现小白字时，**必须按音量加（Volume Up）键才能进入 Superfastboot 模式。**
