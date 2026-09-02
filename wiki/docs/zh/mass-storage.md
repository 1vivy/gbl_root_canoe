# USB Mass Storage

Canoe 可以把一个物理分区导出为一个 USB 磁盘。`persist` 分区的 `/efisp` 下包含
启动根目录；如果存在 `logfs`，它可用于收集日志。应用和 `canoe-bootmgr` 负责导出
流程。不要用第二个工具挂载或编辑正在使用的文件系统。

## 导出前

实时 `persist` 导出是独占的。主机操作导出内容前，请停止 Android，或确认 Android
没有使用 `persist`。主机不得与 Android 并发使用实时 `persist` 导出，以免两个写入者
同时操作同一 ext4 日志与启动根目录文件。

## 开始导出

BDS 菜单仍有 **USB Mass Storage** 操作。从应用的 Guided flow 进入 Commit 阶段时，
应用会请求导出，并且只在安装事务期间保持导出。CLI 也提供相同操作：

```bash
canoe-bootmgr fastboot export --target persist
canoe-bootmgr fastboot export --target logfs
```

响应会给出原始块设备节点。每次会话只导出一个分区。如果已有 Canoe 导出的未挂载
设备，`fastboot.export` 会接管它，不会再启动第二个导出。

一次导出有两种同等、正常的结束方式：由主机结束（boot manager 或 CLI 发出 SCSI eject），或操作员在设备上按音量下。两者都不是失败，也互不构成对另一种方式的替代。

当 Canoe 自带的大容量存储驱动提供该导出时，它会在 gadget 消失前交付 SCSI 应答，随后设备返回发起导出的界面——菜单或 fastboot。这次往返正是设计目的：主机工具可以导出 `persist`、完成工作，再把设备交还原来的界面，而无需操作员触碰手机。

此行为依赖 Canoe 自带驱动实际提供导出。原驻平台驱动从不报告 eject，因此由它提供的会话只能按原来的方式结束。在 SM8850 Canoe 目标设备上，导出枚举为 **`1209:ca0e`**“USB MASS STORAGE”，这是 Canoe 自带驱动的身份，因此主机 eject 在该硬件上是正常路径。

断开或失去 USB 连接不会取消导出；重新连接并完成操作后，再使用上述任一正常结束方式。导出期间 USB 链路是 mass-storage gadget，不提供 fastboot 通道，因此此时发出的 fastboot 命令正常会报告 `waiting-for-any-device`。CLI 可通过 `fastboot.end-export --node <RAW_NODE>` 使用主机结束路径。

较旧的 BDS 构建会在没有界面的情况下启动导出并静默吞掉按键。如果界面没有变化，
正在运行的 BDS 就早于该修复。

## Linux 上的导出身份

Canoe 自带的大容量存储驱动通常枚举为 **`1209:ca0e`**（产品名 `efisp boot root`，
固定磁盘），而非平台的 `05c6:f000`。如果自带驱动无法启动，平台驱动可能回退到
原始身份；源探测器接受两种身份。

没有系统规则认领 `1209:ca0e`，所以自带导出不会被模式切换。在 Linux 上，回退的
`05c6:f000` 可能匹配发行版将其当作 4G 网卡的 `usb_modeswitch` 规则，并在磁盘扫描
前弹出设备。使用这种回退身份时，一次性禁用该切换：

```bash
printf 'DisableSwitching=1\n' | sudo tee /etc/usb_modeswitch.d/05c6:f000
```

应用和 CLI 都使用唯一的 `source.detect` 探测器。它报告候选类型（`block`、`image` 或
`dir`）、路径、身份、型号、大小、启动根目录是否存在、读写能力、权限需求、挂载点和
原因。未挂载且可读的 Canoe 导出会标记为 `export_candidate`。操作超时后可以安全重试：
后续运行会接管 `source.detect` 已报告的磁盘，而不是要求 BDS 再次导出。

```bash
canoe source detect --json
canoe-bootmgr --json source detect
```

原生主机不会自行遍历 sysfs 或查询 PowerShell。访问被拒绝时，应用会在 Linux 提供
明确的提权重试，或在 Windows 提供管理员重启；它不会静默提权。

## 通过导出安装

主机不得挂载导出的文件系统。`canoe-bootmgr` 把选定的原始块源交给基于 libext2fs
的 `canoe-ext4` 后端，由该后端负责锁定、日志恢复、有界写入、刷新和关闭：

```bash
canoe install --slot a --mode 1
```

`canoe` wrapper 会请求 `source.detect`，然后接管可读且未挂载的导出。需要明确选择
源时，使用全局源选项：

```bash
canoe-bootmgr --source <RAW_NODE> install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
```

后端在缺少 `/efisp` 时创建它，并以一个事务提交启动根目录文件、附属文件、配置与
回滚。主机安装器不会刷写分区；带漏洞的 ABL 与 `BDS.efi` 操作仍是安装指南中分开
且需明确执行的 fastboot 操作。

事务完成后，从主机或设备结束导出。使用主机结束路径：

```bash
canoe-bootmgr fastboot end-export --node <RAW_NODE>
```

也可以在设备上按音量下。

如果应用报告导出仍被保持，请完成当前应用流程，不要从另一个终端启动第二次导出。

## Windows 原始磁盘操作

Windows 工具包包含原生 `canoe-bootmgr.exe` 与 `canoe-ext4.exe`。检测到导出的物理磁盘
后，boot manager 会把 `\\.\PhysicalDrive<N>` 源直接交给 helper。不需要安装 Python，
也不需要盘符挂载或第三方文件系统驱动。

Windows 支持显式脏日志恢复：使用 `canoe-ext4.exe --recover`；退出码 4 表示文件系统
脏，恢复从不隐式执行。如果 helper 无法访问磁盘，使用应用明确提供的
**Restart as Administrator** 后重试；不要让 Android 继续使用实时导出。

配置格式见规范版[`canoe.cfg 契约`](./canoe-cfg.md)。OTA 与模式变更请使用应用的
Guided flow，让导出、证据、确认、写入和结束步骤保持在一个经过审核的序列中。
