# 卸载操作指南

Canoe 没有用于彻底移除启动链的单一协议操作或 `canoe` 子命令。应用可以在
**Entries** 中通过 `entry.remove` 删除单个已保存启动项，但彻底移除启动链仍
必须删除 `canoe.cfg`，并使用平台 fastboot 工具擦除原始 `efisp` 分区。不要把删除
一行当作卸载。

## 1. 备份数据

移除启动链前先备份重要数据。解锁状态、Recovery 行为和数据访问方式取决于具体设备。
如需先查看当前配置，可使用应用的 **Entries** 与 **Settings** 界面，或通过
`config.show` 读取；不要把缺少某一行误认为启动根目录为空。

## 2. 移除启动根目录配置

使用以下任一受控路径：

- 进入能够挂载 `persist` 的 Recovery，删除 `/persist/efisp/canoe.cfg`；或
- 从应用或 CLI 启动 BDS **USB Mass Storage** 导出，停止 Android 对 `persist` 的使用，
  并使用随工具发布的 `canoe-ext4` 工具操作未挂载的导出卷。不得在 Android 同时使用
  实时导出。写入刷新后，在设备上按音量下结束导出。

CLI 导出原语为：

```bash
canoe-bootmgr fastboot export --target persist
```

对于原始节点为 `<RAW_NODE>` 的 `persist` 导出卷，只删除配置文件：

```bash
canoe-ext4 remove <RAW_NODE> /efisp/canoe.cfg
```

应用的导出/安装路径会在事务前后使用 `fastboot.export` 与 `fastboot.end-export`。运行
中的 Android 中该文件是 `/mnt/vendor/persist/efisp/canoe.cfg`，导出卷上的路径是
`\efisp\canoe.cfg`。删除文件会阻止 BDS 使用已配置的受管理启动项，但不会擦除
`persist` 文件系统。

实时导出是没有 fastboot 通道的 mass-storage gadget。导出期间，主机 fastboot 命令
无法到达 BDS。**设备上的音量下是结束导出的唯一契约控制。**

## 3. 擦除 BDS

进入官方 fastboot，使用平台工具擦除原始 BDS 分区：

```bash
fastboot erase efisp
```

应用的 `fastboot.flash` 操作只会刷写明确指定的镜像，不能擦除分区。
`canoe-bootmgr` 没有协议擦除操作，因此不要寻找应用内擦除按钮，也不要自行编造
协议请求。

如果官方 fastboot 不可用而 BDS 仍在运行，也可以通过 BDS fastboot 服务执行同一个
外部命令：

```bash
fastboot erase efisp
```

如果计划硬件回锁，应在启动链仍存在时完成回锁，再擦除 `efisp`：

```bash
fastboot flashing lock
fastboot erase efisp
```

真正的硬件回锁可能需要厂商账号或设备专用的解锁流程。回锁会触发平台的数据清除行为。

## 4. 可选的数据清除

如果设备的卸载流程要求清空数据分区：

```bash
fastboot -w
```

这会删除用户数据，请先确认备份有效。

## 结果

删除 `canoe.cfg` 并擦除原始 `efisp` 后，BDS 启动链不再可用，设备将遵循剩余的
厂商软件和实际 Bootloader 状态。应用和 CLI 只有在各自的 `entry.remove`、导出或
写入器响应成功后才会报告成功；这些操作本身不能替代最后的外部擦除。
