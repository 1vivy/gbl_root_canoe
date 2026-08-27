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
导出界面。每次会话只作为一个 USB LUN 导出一个分区。结束会话前，必须完成
所有写入并卸载文件系统。

**两条路径都必须在设备上按音量下结束会话，包括 fastboot oem 路径。**断开或
失去 USB 连接不会取消会话；重新连接并完成卸载后，再在设备上按音量下。

较旧的 BDS 构建会在没有界面的情况下启动 oem 导出，并静默吞掉按键。如果
界面没有变化，正在运行的 BDS 就早于此修复。

**Linux：usb_modeswitch 会终止导出。** 设备枚举为 `05c6:f000`，发行版自带
的 udev 规则会把它当作需要模式切换的 4G 网卡并弹出设备：`usb-storage`
在内核扫描前被卸载，弹出操作同时结束设备端的会话。如果磁盘一直不出现
（并且手机退回 fastboot 菜单），一次性禁用该切换：

```bash
printf 'DisableSwitching=1\n' | sudo tee /etc/usb_modeswitch.d/05c6:f000
```

在规则仍然生效的主机上，`canoe install` 等待磁盘超时会直接给出这一处置
方法。

`canoe install` 通过 `05c6:f000` 这一 USB 身份识别 LUN，而不是等待新的磁盘
名出现；因此超时或中断过的运行可以直接对同一个仍在进行的会话重试：它会接管
总线上已经存在的磁盘，而不是再次请求导出——BDS 处于导出循环中时并不会响应
fastboot。它还会卸载桌面自动挂载程序抢占的那一份挂载（GNOME 与 KDE 会在
LUN 枚举时立即把它挂载到 `/run/media` 下），因为按音量下之前必须完成的刷新
与卸载由安装器负责。

## 通过导出执行电脑端安装

在 Linux 上，可以让 `canoe install` 执行导出与挂载，或传入已经挂载的启动
根目录：

```bash
canoe install --slot a --mode 1
canoe install --boot-root /path/to/persist/efisp --slot a --mode 1
```

电脑端安装器只会在已挂载的 `persist/efisp` 中提交事务，不会刷写分区。漏洞
ABL 与 `BDS.efi` 的 fastboot 命令仍由操作员按 [`install.md`](./install.md)
中的说明自行完成。

## Windows 挂载与安装

Windows 压缩包附带 `fastboot.exe`、`ext4windows.exe`、`winfsp-x64.dll` 和
WinFsp 安装程序。Ext4Windows 默认只读；启动根目录事务必须指定 `--rw`。

1. 执行 `fastboot oem mass-storage:persist`，或在 BDS 菜单选择
   **USB Mass Storage** 并确认警告。
2. 在管理员 PowerShell 中记录导出前后的 USB 磁盘：

   ```powershell
   Get-Disk | Where-Object BusType -eq 'USB' | Select-Object -ExpandProperty Number
   ```

   选择新出现的物理编号，不要选择导出前已经存在的磁盘。
3. 运行 `ext4windows.exe status`，从 `Z:` 向下选择第一个空闲盘符。
4. 以读写方式挂载导出的磁盘：

   ```text
   ext4windows.exe mount \\.\PhysicalDrive<N> Z: --rw
   ```

5. 以启动根目录为目标执行安装：

   ```text
   canoe.cmd install --boot-root Z:\efisp --slot a --mode 1
   ```

6. 刷新安装并卸载卷：

   ```text
   ext4windows.exe unmount Z:
   ```

7. 在**设备上按音量下**。这是唯一的会话取消控制；仅卸载不会结束 BDS 导出。

如果压缩包工具挂载失败，按以下方式恢复：运行 `ext4windows.exe --scan`，
手动挂载卷，然后重新运行
`canoe.cmd install --boot-root <drive>:\efisp --slot a --mode 1`。

配置格式见规范版 [`canoe.cfg 契约`](./canoe-cfg.md)。
