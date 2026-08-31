# USB Mass Storage 指南

Super Fastboot 可以将一个物理分区作为 USB 磁盘导出。`persist` 分区的
`/efisp` 下包含启动根目录；如果存在 `logfs`，也可以用它收集日志。

## 开始导出

在 BDS 菜单选择 **USB Mass Storage**，选择 `persist`，并确认正在使用中的
文件系统警告。也可以在 fastboot 中启动相同操作：

```bash
fastboot oem mass-storage             # persist（默认）
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

无论是设备菜单路径还是 `fastboot oem mass-storage:persist` 路径，都会显示
导出界面。每次会话只作为一个 USB LUN 导出一个分区。

**两条路径都必须在设备上按音量下结束会话，包括 fastboot oem 路径。**断开或
失去 USB 连接不会取消会话；重新连接并完成操作后，再在设备上按音量下。

较旧的 BDS 构建会在没有界面的情况下启动 oem 导出，并静默吞掉按键。如果
界面没有变化，正在运行的 BDS 就早于此修复。

**导出的身份。** BDS 自带大容量存储驱动，按需启动：设备枚举为
**`1209:ca0e`**（产品名 `efisp boot root`，固定磁盘），而不是平台的
`05c6:f000`。没有任何系统规则认领这一身份，因此自带驱动的导出不会再遇到
下面的 modeswitch 问题；当自带驱动无法启动时，由平台自身的驱动以原始呈现
接管，两种身份工具都会识别。

**Linux：usb_modeswitch，仅在回退身份上。** 没有任何规则认领
`1209:ca0e`，因此自带驱动的导出不会被干扰。当会话回退到平台驱动的
`05c6:f000` 时，发行版自带的 udev 规则会把它当作需要模式切换的 4G 网卡：
`usb-storage` 在内核扫描前被卸载。一次性禁用该切换：

```bash
printf 'DisableSwitching=1\n' | sudo tee /etc/usb_modeswitch.d/05c6:f000
```

`canoe install` 会请求 `canoe-bootmgr source detect --json`，选择第一个身份为
`1209:ca0e` 或兼容身份 `05c6:f000`、可读且未挂载的 block 行。这是唯一的
USB 源探测实现；原生主机不再遍历 sysfs 或查询 PowerShell。超时或中断后，
再次运行会接管 `source detect` 已报告的同一磁盘，而不是在 BDS 导出循环中再次
请求导出。

## 通过导出执行电脑端安装

主机不会挂载导出的文件系统，而是把选定的原始块设备直接交给
`canoe-bootmgr`。其基于 libext2fs 的 `canoe-ext4` 后端负责独占锁、日志恢复、
有界写入、刷新和关闭：

```bash
canoe install --slot a --mode 1
```

如果缺少 `/efisp`，helper 会创建它，然后 boot manager 通过同一后端提交启动
根目录文件、附属文件、配置和回滚。电脑端安装器不会刷写分区；漏洞 ABL 与
`BDS.efi` 的 fastboot 命令仍由操作员按 [`install.md`](./install.md) 的说明
自行完成。

对于非实时导出的 ext4 镜像或原始块源，显式选择直接后端：

```bash
canoe-bootmgr --source /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
canoe-bootmgr --ext4-image /path/to/persist.ext4 install \
  --staged /path/to/staged --slot a --mode 1
```

双语 `canoe-gui` 通过 `--source`/`--ext4-image` 或 `--boot-root` 提供相同
选择；后端对比见 [`install.md`](./install.md)。

测试或操作员自行管理的本地目录可以使用显式 local 后端：

```bash
canoe install --boot-root /path/to/persist/efisp --slot a --mode 1
```

## Windows 原始磁盘安装

Windows 压缩包附带原生 `canoe.exe`、`canoe-bootmgr.exe`、
`canoe-ext4.exe` 和 `fastboot.exe`。选择新枚举的 USB 物理磁盘后，boot manager
将其 `\\.\PhysicalDrive<N>` 原始路径直接交给 helper：

```text
canoe.exe install --slot a --mode 1
```

不需要安装 Python，也不再捆绑解释器或使用启动脚本。不使用盘符挂载或第三方
文件系统驱动。打包时必须提供 `canoe-ext4.exe`；如果当前主机无法原生构建，
可运行 `tools/canoe-ext4/build-windows.sh` 后将输出传给打包输入覆盖参数。
缺少该输入会使构建失败，不会静默删除 Windows 支持。

操作完成后在**设备上按音量下**，这是唯一的会话取消控制。

配置格式见规范版 [`canoe.cfg 契约`](./canoe-cfg.md)。
## 探测与连接源
`canoe-bootmgr source detect --json` 是只读的，枚举候选源不需要提权。每行会
报告源类型（`block`、`image` 或 `dir`）、路径、身份、型号、大小、启动根目录
是否存在、读写能力、`needs_privilege`、挂载点和原因。原生主机不遍历 sysfs
或查询 PowerShell；USB 源探测只有这一份实现。示例空结果为：

```json
{"ok":true,"kind":"source.detect","sources":[]}
```

图形 Connect 界面使用相同结果，支持一键连接、Refresh 以及手动目录/镜像/设备
选择。目录和镜像不需要提权。访问被拒绝时，Linux 提供 **Retry with pkexec**
和可复制的 `sudo` 命令，Windows 提供 **Restart as Administrator**，不会静默
提权。

Windows 支持显式脏日志恢复：使用 `canoe-ext4.exe --recover`；退出码 4 表示
文件系统脏，恢复从不隐式执行。Windows helper 链接了日志回放所需的
e2fsprogs journal/revoke/recovery 对象。
