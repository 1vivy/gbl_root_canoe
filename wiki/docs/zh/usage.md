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

如果启动根目录中既没有 `canoe.cfg` 也没有 `boot.efi`，BDS 会显示首次运行
信息并进入 Super Fastboot。此时没有可启动的启动项。

### 如何进入菜单，而不是直接启动默认项

BDS 在启动时会**先采样音量上键一秒**，早于文件系统栈的初始化，以免初始化过程
吃掉这次按键。

- **在这一秒内按住音量上**：立即进入菜单，不会尝试任何无人值守启动。
- **未按住音量上**：BDS 直接启动 `canoe.cfg` 中 `default` 指定的启动项，不显示
  菜单。只有当该启动返回或失败、或文件中没有 `default` 时，才会进入菜单。

进入菜单后，倒计时**只作用于第一次绘制**：等待 `timeout` 秒后启动当前高亮的
默认项。任意按键都会取消倒计时，之后每次绘制都无限等待。`timeout 0` 并不表示
“永久等待”，而是立即启动默认项；因此用于测试新启动项的配置应当给一个充裕的
`timeout`，而不是 0。

新增手写启动项时这一点尤其重要：一个因为默认项先启动而始终没被选中的启动项，
看起来和一个根本没被枚举出来的启动项完全一样。

菜单包括：

- **Reboot to Recovery**；
- **USB Mass Storage**；
- 文件存在时显示受管理的 `Android (slot A)`、`Android (slot B)` 与
  `Android (previous)`；
- `canoe.cfg` 中其他所有 `image` 存在的启动项，包括手写的第三方固件链式启动项
  ——见[链式启动第三方 UEFI 固件](./chainload.md)；
- 启动根目录 `tools/` 中文件对应的 **EFI Tools**。

随附的 `SurfaceTools.efi` 清单工具可从 **EFI Tools** 打开。默认视图只枚举
UEFI 协议 GUID、配置表 GUID、已加载镜像类别、内存描述符和已知的 Qualcomm
策略协议是否存在；不会显示原始地址，也不会调用厂商方法。**Dump Passive
Inventory to logfs** 会明确覆盖已挂载 `logfs` 卷上的
`\SurfaceTools.log`，刷新文件内容并在返回 BDS 前关闭全部文件句柄。执行
**Run Read-only Active Probes** 前必须再次按音量加键确认（电源键用于取消，
因此长按菜单选择键不会授权调用）；该操作只调用五个已记录的读取方法，用于
查询 CPU 最大索引、TrustZone 版本、Verified Boot 状态和 Keymaster 状态。
调用成功时显示 `authorized`；工具不会据此推断策略已实际生效，且主动读取
方法不会写入持久状态。

USB Mass Storage 会将一个分区作为一个 USB 磁盘导出。`persist` 的
`/efisp` 中包含启动根目录；仅当 `logfs` 存在时才会提供它。导出正在使用的
`persist` 文件系统前，BDS 会显示警告。电脑端流程见
[`mass-storage.md`](./mass-storage.md)。

也可以在 fastboot 中导出：

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

这里提供 Recovery 是因为启动菜单中的同名项在 fastboot 中无法到达：菜单在
fastboot 循环启动之前运行，而启动根目录为空的设备根本不会显示菜单。导出会话
结束、安装完成后要进入 recovery，就用这一行。

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
- **Mode 2** 还使用匹配的 120 字节 `boot.efi.gm2p` profile 和生成的映射。
  它通过内核命令行禁止 `oplus_secure_guard_new`，无需重新打包 boot 镜像。

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
