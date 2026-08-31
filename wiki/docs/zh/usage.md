# Super Fastboot 使用指南

## 进入 Super Fastboot

- 将 BDS 临时启动到内存，不写入闪存：

  ```bash
  fastboot stage <BDS.efi>
  fastboot oem boot-efi
  ```

- 启动时出现 OEM 解锁警告后，按**音量上**进入 Super Fastboot。

### 两条内存启动路径都可用，原理不同

```bash
fastboot stage <BDS.efi>
fastboot oem boot-efi
```

```bash
fastboot boot <BDS.efi>
```

第二条常令人意外，因为 `BDS.efi` 是 PE 镜像而不是 Android boot 镜像。它之所以
可行，是因为**电脑端**的 `fastboot` 工具会先把传入的文件包装成一个合成的 boot
镜像再发送——命令回显本身就说明了这一点：

```text
creating boot image...
creating boot image - 440320 bytes
Sending 'boot.img' (430 KB)                        OKAY
Booting                                            OKAY
```

输入 438272 字节的 `BDS.efi`，输出 440320 字节的 boot 镜像。随后 bootloader 的
boot 处理函数在该镜像 kernel 段的开头识别出 `MZ` 签名，于是按 EFI 载荷启动而不
是当作内核处理；这正是 `QcomModulePkg/Library/FastbootLib` 中 `IsEfiInBootImg`
的用途。在一加 15 上实测如此。

两者任选。`fastboot stage` + `fastboot oem boot-efi` 是显式形式，不依赖电脑端
工具的包装行为；`fastboot boot` 只需一条命令。

## 首次运行与菜单

`SfbBootRootIsEmpty` 会将无法定位或打开卷、没有可启动镜像的启动根目录，以及
所有 `image` 都不存在的配置视为首次运行。BDS 显示首次运行界面，其中有两行：

- **Enter boot menu (Volume Up)**
- **Enter fastboot (default)**

光标从 **Enter fastboot** 开始，界面等待两秒。只有音量上会选择进入普通菜单；
超时、音量下和电源键都会保留 fastboot 默认路径。这是刚刷入 BDS 后的安全路径：
电脑端无需先准备菜单配置就能安装启动链。

对于已填充的启动根目录，BDS 会在启动时按 `menu-mode` 读取策略，并采样
`key-window` 毫秒：

- **Silent 模式**（新安装默认）：窗口内按音量上会打开菜单并无限等待；音量下
  进入现有 fastboot 路径；没有按键则立即启动配置的默认项。
- **Menu 模式**：窗口内按音量下进入 fastboot，随后总是打开菜单。菜单按
  `menu-timeout` 秒倒计时后启动默认项；任意按键会取消倒计时并使菜单无限等待。

`key-window` 范围为 `0..=10000` 毫秒，默认 `1200`；零表示不采样。
`menu-timeout` 范围为 `0..=300` 秒，默认 `5`，仅 Menu 模式生效；零表示永不
自动启动。默认项无法解析（包括未发现的 `bls:<stem>`）时，BDS 显示现有
拒绝/提示界面并等待，不会回退到其他启动项。

默认项可以是 Android 行，也可以是发现的 BLS Type #1 行，例如
`default bls:pmos`。BLS 默认项是直通行（无附属文件、钩子或槽位语义），USB
上的 BLS 行不能作为无人值守默认项。
`canoe-bootmgr --json config show` 会返回 `menu_mode`、`key_window_ms` 与
`menu_timeout_s`；不再返回旧的 `timeout_seconds` 字段。


菜单按以下顺序构建：

1. **Session boot mode**（只对下一次启动生效，不会保存）。
2. `canoe.cfg` 中 `image` 存在的启动项。
3. 配置缺失或无效时，探测启动根目录中的兼容路径 `boot.efi`、按槽位命名的
   `boot_a.efi` 与 `boot_b.efi`，以及 `boot_backup.efi`。当前安装器只写按槽位
   命名的文件与备份；`boot.efi` 只是 b2 以前的兼容探测。
4. 每个卷上的 `\EFI\BOOT\BOOTAA64.EFI`。若存在 `\EFI\DESC` 则用它命名，否则
   使用 `NONAME<n>`。
5. `persist` ext4 启动根目录或可移动介质中 `\loader\entries\*.conf` 下的有效
   BLS Type #1 启动项。见[链式启动与 BLS 启动项](./chainload.md)。
6. 内置操作：**Enter Fastboot**、**Enter EFI Program Selector**、**EFI Tools**、
   **USB Mass Storage**、**Reboot to Recovery**、**Power Off** 与 **Restart**。

只有镜像存在时才显示配置行；镜像缺失会被跳过。BLS 文件无效或引用镜像缺失时
也会被跳过。只有设备启动根目录中发现的 BLS 行，且 `default bls:<stem>` 指向
已发现的 stem 时，才可作为无人值守默认项。该 BLS 默认项保持直通，不使用附属
文件、钩子或槽位语义；USB 上的 BLS 行仍不符合条件。交互启动 BLS 时，按住
音量上进入菜单，移动到该行后按电源键。

同一菜单还提供以下工具与操作：

- **EFI Tools** 会列出启动根目录 `tools/` 下的文件。
- **USB Mass Storage** 将一个分区导出为一个 USB 磁盘。`persist` 的 `/efisp` 中
  有启动根目录；只有存在 `logfs` 时才提供它。导出正在使用的 `persist` 前 BDS
  会警告。详见 [`mass-storage.md`](./mass-storage.md)。
- **Reboot to Recovery** 直接重置进入 Recovery。这是内置重置操作，不是自定义镜像
  解析器。

随附的 `SurfaceTools.efi` 清单工具可从 **EFI Tools** 打开。默认视图只枚举 UEFI
协议 GUID、配置表 GUID、已加载镜像类别、内存描述符和已知 Qualcomm 策略协议是否
存在；不会显示原始地址，也不会调用厂商方法。**Dump Passive Inventory to logfs**
会明确覆盖已挂载 `logfs` 卷上的 `\SurfaceTools.log`，刷新文件内容并在返回 BDS 前
关闭全部文件句柄。执行 **Run Read-only Active Probes** 前必须再次按音量加键确认
（电源键用于取消，因此长按菜单选择键不会授权调用）；该操作只调用五个已记录的
读取方法，用于查询 CPU 最大索引、TrustZone 版本、Verified Boot 状态和 Keymaster
状态。调用成功时显示 `authorized`；工具不会据此推断策略已实际生效，且主动读取
方法不会写入持久状态。

USB Mass Storage 会将一个分区作为一个 USB 磁盘导出。也可以在 fastboot 中导出：

```bash
fastboot oem mass-storage             # persist（默认）
fastboot oem mass-storage:persist     # persist
fastboot oem mass-storage:logfs       # logfs
```

每次会话只导出一个分区。**结束 Mass Storage 会话的唯一方式是设备上的音量下**。
断开数据线不会结束会话。

## Fastboot 模式界面

Super Fastboot 等待主机时会显示自己的选项，用音量上/下移动，电源键选择：

- **Stay in Fastboot**——空操作，仅重绘；光标默认停在这一行，因此误触不会
  产生任何后果；
- **Reboot to Recovery**；
- **Power Off**；
- **Restart**。

这里提供 Recovery 是因为进入 fastboot 后无法重新进入启动菜单：启动菜单在
**Enter Fastboot** 之前运行，而首次运行也默认直接到此界面。完成电脑端安装或
导出会话后要进入 Recovery，就用这里的 **Reboot to Recovery**。

电脑端的重启目标现在会被遵守：

```bash
fastboot reboot              # Android
fastboot reboot recovery     # recovery
fastboot reboot bootloader   # 回到 Super Fastboot
```

其他目标会直接失败，而不是重启到未被指定的位置；本设备没有用户空间
fastbootd，因此 `fastboot reboot fastboot` 会被拒绝，而不会被当作重启到
bootloader。

结束导出本身仍然要在设备上按音量下。导出进行时 USB 链路是 mass storage
gadget，不承载 fastboot 通道，任何主机命令都到不了 BDS。主机发出的 SCSI
eject 在本硬件上确实会结束会话，但那是厂商栈的副作用而非契约，canoe 不依赖它。

## 模式与 DeviceInfo

菜单中的模式选择是下一次启动的临时覆盖，绝不会保存。启动项自身的模式优先，
文件全局 `mode` 作为回退；详见 [`canoe-cfg.md`](./canoe-cfg.md)。

- **Mode 0** 是不启用 hook 的直通模式，不读取也不写入 `DeviceInfo`；
- **Mode 1** 投射锁定的 DeviceInfo 视图并应用受管理 hook；
- **Mode 2** 还使用受管理 `boot_a.efi`、`boot_b.efi` 或 `boot_backup.efi` 对应的
  120 字节 `.gm2p` profile 和生成的映射。它通过内核命令行禁止
  `oplus_secure_guard_new`，无需重新打包 boot 镜像。

Mode 1 或 Mode 2 在观测状态不满足策略时可以修复 `DeviceInfo`。
`devinfo-repair never` 会拒绝修复并如实以 Mode 0 继续，`asneeded` 允许修复。
启动日志会记录观测状态与采取的动作。

Mode 2 profile 只能证明 `vbmeta` 已解析并带有签名和公钥 blob，不能证明密钥属于
OEM，本工具无法证明这一点。自动保护只检测公钥摘要是否相对于已安装世代发生变化。

## Bootloader 命令

回锁 Bootloader 会触发平台的数据清除行为：

```bash
fastboot flashing lock
```

不清除数据的解锁方式：

```bash
fastboot flashing unlock
fastboot flashing unlock_critical
```

TEE 状态不一致时，设备可能拒绝提供数据密钥。

## 刷写与擦除

```bash
fastboot flash <partition> <file.img>
fastboot erase <partition>
```

操作员先将带漏洞的 ABL 刷入 `abl`，再将 `BDS.efi` 刷入 `efisp`；电脑端安装器
不会写入分区。

## 重启

```bash
fastboot reboot bootloader
fastboot reboot
```

此 BDS 的 fastboot `reboot` 处理器只支持 **Normal** 模式。
`fastboot reboot recovery` 在这里不是进入 Recovery 的命令；请在 BDS 菜单选择
**Reboot to Recovery**，或通过 **EFI Tools** 打开 Recovery 启动项。
## 启动策略与源探测

电脑端 `canoe` 将策略修改转交给唯一的 `canoe-bootmgr` 写入器：

```bash
canoe config set-policy [--menu-mode silent|menu] \
  [--key-window-ms N] [--menu-timeout-s N]
canoe default set android-a
canoe default set bls:pmos
canoe source detect --json
```

`default set bls:<stem>` 只有在与 `bls list` 相同的发现流程找到该 BLS 行时
才会接受。目录或镜像源不需要提权；设备访问被拒绝时，Linux 显示
**Retry with pkexec** 及可复制的 `sudo` 命令，Windows 显示 **Restart as
Administrator**，都不会静默提权。

Linux 工具包可从任意当前目录双击根目录的 `canoe-gui` 启动器；Windows 双击
工具包根目录的 `canoe-gui.exe`（无控制台窗口，辅助程序仍在 `bin/`）。
Connect 界面运行 `source detect`，显示路径、身份、型号、大小、启动根目录、
原因和提权需求，并提供一键连接、Refresh 以及手动目录/镜像/设备选择，同时
记住平台配置目录中的上次成功源。
