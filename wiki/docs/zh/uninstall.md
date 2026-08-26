# 卸载操作指南

## 1. 备份数据

进行任何卸载操作前，务必完整备份重要数据，避免数据丢失。

## 2. 硬件真回锁要求

若 Bootloader 已被**硬件真回锁**（真正执行过 `fastboot flashing lock`，而不是 BDS 的启动策略），必须先解锁 BL。请使用设备支持的解锁途径，例如厂商账号解锁流程，或在启动链仍在时通过 Superfastboot 执行 `fastboot flashing unlock`。只有在真回锁发生时启动链仍然存在，reserve token 保护机制才可能保留这条解锁路径。

## 3. 移除启动根目录与 BDS

1. 进入**官方 fastboot**，或进入能够访问 `persist` 的 Recovery。
2. 如果启动根目录中仍有 `canoe.cfg`，先将其删除：
   - 已启动的 Android：`/mnt/vendor/persist/efisp/canoe.cfg`
   - Recovery 或已导出的 `persist`：`/persist/efisp/canoe.cfg`
3. 擦除原始 BDS 分区：

   ```bash
   fastboot erase efisp
   ```

4. 如果设备的卸载流程要求清除用户数据，再执行：

   ```bash
   fastboot -w
   ```

删除 `canoe.cfg` 即可清除 7.x 的启动根目录配置；不再有需要单独清除的加载器策略。如果在擦除原始分区前无法访问启动根目录，`efisp` 被擦除后该配置也不会再被使用，但条件允许时仍应通过 Recovery 或 USB Mass Storage 将其删除。

## 4. 没有官方 fastboot 时

如果设备唯一可用的 fastboot 是 BDS 提供的 Superfastboot，请在同一次会话中完成硬件真回锁和启动链擦除：

1. 从 BDS 进入 Superfastboot。
2. 按顺序执行：

   ```bash
   fastboot flashing lock
   fastboot erase efisp
   ```

请保持这个顺序。回锁必须在启动链仍存在时完成；如果先擦除 `efisp`，可能会失去设备支持的回锁路径。如果能先进入 Recovery 或 USB Mass Storage，请按上文说明从 `persist/efisp` 删除 `canoe.cfg`。

## 注意事项

- 操作前确认设备对应的 BL 要求。
- `fastboot -w` 会清除数据分区，请确认重要文件已经备份。
- 卸载后，BDS 启动链会被移除，设备将根据其余软件状态恢复为正常的解锁/root 状态。
