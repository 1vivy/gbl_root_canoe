# 卸载操作指南

## 1. 备份数据

移除启动链前先备份重要数据。解锁状态、Recovery 行为和数据访问方式取决于
具体设备。

## 2. 移除启动根目录配置

可以使用以下任一路径：

- 进入能够挂载 `persist` 的 Recovery，删除
  `/persist/efisp/canoe.cfg`；或
- 在 BDS 中选择 **USB Mass Storage**，导出 `persist`，在电脑端挂载后删除
  `efisp/canoe.cfg`，刷新写入并卸载。

已启动的 Android 系统中，同一文件路径为
`/mnt/vendor/persist/efisp/canoe.cfg`。删除该文件会阻止 BDS 使用受管理启动
项，但不会擦除 `persist` 文件系统。

完成编辑后，在**设备上按音量下**结束 BDS Mass Storage 会话。这是唯一的会话
取消控制。

## 3. 擦除 BDS

进入官方 fastboot，擦除原始 BDS 分区：

```bash
fastboot erase efisp
```

这会移除 `BDS.efi`，但不会格式化或替换 `persist`。如果官方 fastboot 不可用
而 BDS 仍在运行，可使用 BDS fastboot 服务：

```bash
fastboot flashing lock       # 只有计划硬件回锁时执行
fastboot erase efisp
```

如果要硬件回锁，必须保持这个顺序：在启动链仍存在时完成回锁，再擦除
`efisp`。真正的硬件回锁可能需要厂商账号或设备专用的解锁流程。

## 4. 可选的数据清除

如果设备的卸载流程要求清空数据分区：

```bash
fastboot -w
```

这会删除用户数据，请先确认备份有效。

## 结果

删除 `canoe.cfg` 和原始 `efisp` 后，BDS 启动链不再可用，设备将遵循剩余的
厂商软件和实际 Bootloader 状态。
