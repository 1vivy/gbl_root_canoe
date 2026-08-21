# 卸载操作指南


## 1. 备份数据

在进行任何卸载操作之前，务必**先完整备份重要数据**，避免数据丢失。


## 2. 硬件真回锁要求

若 Bootloader 已被**硬件真回锁**（通过 `fastboot flashing lock` 执行的真实回锁，并非 BDS 启动模式），必须**先解锁 BL**，再进行后续操作：

| 设备 | 解锁方式 |
|------|----------|
| **Twoplus** | 深度测试解锁 |
| **Dami** | Super Fastboot 解锁 |


## 3. 卸载步骤

1. 进入**官方 fastboot** 模式

2. 擦除补丁分区：

   ```bash
   fastboot erase efisp
   ```
   也可以在adb shell su -c下执行

   ```bash
   adb shell su -c dd if=/dev/zero of=/dev/block/by-name/efisp
   ```

3. 格式化并清除用户数据：

   ```bash
   fastboot -w
   ```


## ⚠️ 注意事项

- 📌 操作前请确认已按对应机型要求**完成 BL 解锁**
- 📌 `fastboot -w` 将**清除数据分区**，请务必提前备份重要文件
- 📌 卸载完成后，设备将恢复至**解锁root状态**
