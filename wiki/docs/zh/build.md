# 构建指南

## 构建发布包

在仓库根目录执行：

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Android 工具包和模块构建要求 `NDK_PATH` 指向 Android NDK。工具包归档位于各
`targets/toolkit_*/build/` 目录，模块归档位于
`targets/magisk_module/build/`。

## 工具包工具

Linux 和 Android 工具包包含 `extractfv`、`patch_abl`、`mode2_profile` 和 `abl_tzmap`。Windows 工具包包含 `extractfv.exe`、`patch_abl.exe`、`mode2_profile.exe` 和 `abl_tzmap.exe`。`abl_tzmap` 从**未修补 ABL**在本地生成并验证 256 字节 `GTZM` `boot.efi.tzmap` TrustZone 接口映射。

Linux 工具包还额外附带来自 `tools/vbmetafixer` 的 `vbmetaport` 与 `vbmetabackup`，Windows 工具包附带其 `.exe` 版本；电脑端安装脚本用它们把官方 vbmeta 移植到第三方 Recovery 镜像上。`vbmetabackup -f <image>` 直接从本地固件镜像完成该提取，无需设备、无需 adb；不带 `-f` 时仍保持原有行为，即通过 adb 拉取设备上的实际链。

## 生成 ABL/profile/map 配对

解压对应平台的工具包，并放入同一套匹配的原厂镜像：

```text
images/abl.img
images/vbmeta.img
```

Linux 工具包运行 `./canoe_build`，Windows 工具包运行 `canoe_build.cmd`。
Android 工具包保留 `build.sh`：它在设备端执行，而设备上不保证提供
`python3`。主机实现会修补 ABL，从匹配的根 vbmeta 镜像生成精确 120 字节的
`efisp/boot.efi.gm2p` KeyMint profile，并从未修补 ABL 在本地生成
256 字节的 `GTZM` `efisp/boot.efi.tzmap` TrustZone 映射。`.tzmap` 与启动镜像并列
存放于 `/mnt/vendor/persist/efisp/boot.efi.tzmap`，不包含在工具包发布压缩包
内。安装时必须同时复制 `efisp/boot.efi` 和精确 120 字节的 `.gm2p`
sidecar；`.tzmap` 在运行时是可选的，因为 BDS 内置了回退映射。

构建脚本会向 `abl_tzmap` 传入 `--allow-incomplete`，因此即使 ABL 没有已记录的
逆向分析证据，仍会得到带有标识符标志和协议命令表的有效 256 字节 sidecar。
安装不会因缺少该证据而失败。

## 电脑端安装工具

主机驱动现在由两个工具包共享同一份 Python 实现。这样不再需要维护两份驱动，
也避免了 Windows 版本反复因 `cmd.exe` 解析细节（而非安装逻辑）出错。
Linux 主机运行时要求 Python 3.11+；Windows 无需安装任何东西，因为工具包在
`python/` 下附带 embeddable CPython。
仓库中的源代码位于 `tools/canoe-host/`；每个工具包压缩包都在
`canoe/` 下附带同一份实现。

两个工具包都附带以下参数完全一致的主机启动器：

| Linux | Windows | 作用 |
|------|---------|------|
| `canoe_prep_device` | `canoe_prep_device.cmd` | 独立准备：从设备拉取 `abl` + `vbmeta` 并派生三件套 |
| `canoe_prep` | `canoe_prep.cmd` | 固件包准备：移植第三方 Recovery 的 vbmeta，并将准备好的镜像替换进固件包 |
| `canoe_stage` | `canoe_stage.cmd` | 电脑端驱动：校验、暂存到启动根目录、调用设备端事务 |

详见各压缩包内的 `README.canoe.md` 与《安装指南》。
`canoe_device_install.sh` 位于 `tools/canoe-device/`，仍然是 shell 脚本，
因为它是在设备端执行的唯一安装事务实现，而设备上不保证提供 Python。

所有设备端绝对路径都通过参数传入，这也正是它能直接在电脑上被测试的原因。
所有主机工具都不触碰 `abl` 分区：让该分区带上 GBL 漏洞是独立的
`fastboot flash abl` 步骤。

固定测试：

- `targets/toolkit_linux/tests/test_canoe_device_install.sh` 直接在普通目录与
  一个代替块设备的普通文件上驱动事务，并通过遮蔽 `mv`、`dd`、`cmp` 注入提交、
  写入与校验失败。
- `targets/toolkit_linux/tests/test_canoe_scripts.sh` 借助 `tests/stub_adb.py`
  驱动两条准备路径与 staging 驱动。

两者都已注册进 `make test`。

## `patch_abl` 修改了什么

`libavb_force_success` 是强制项——缺少它则整个修补失败。其余都是尽力而为、仅告警，因为失败只损失功能而不影响可启动性：

- **锁定状态 fastboot 门控。** ABL 收到的是锁定视图，因此其 fastboot 命令分发会拒绝 `flash`、`erase`、槽位切换和快照取消。每条拒绝消息都锚定到守护它的门控，该门控会被改为无条件跳转或直接移除。全有或全无：若镜像中存在的任一门控无法解析，则不写入任何内容——一个接受 `flash` 却仍拒绝 `erase` 的 ABL 比两者都拒绝更糟。`patch_log.txt` 会列出每个被改写的偏移。
- **Oplus 橙色状态警告**与**强制启用 fastboot**——外观与易用性，Oplus 专有。

出现 `Warning: Failed to patch ABL GBL` 表示该 ABL 不含该漏洞，必须将 `abl` 分区降级。

## 开发者注意事项

编辑 UEFI 源码后，请使用 `UEFI_REBUILD=1 make target_<name>` 重建 BDS，
或者先运行 `make clean`。

本项目不再提供单独的通用构建。
