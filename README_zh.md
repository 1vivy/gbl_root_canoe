# GBL Root Canoe

[English](README.md)

GBL Root Canoe 是一个基于 EDK2 的工作区，用于修补高通 ABL 镜像中的 EFI
程序。它在骁龙 8 Gen 5 / 8 Elite 设备上实现假回锁状态：硬件仍处于解锁，
但已修补的加载器向软件呈现所需的锁定状态。

## 启动链

```mermaid
graph LR
  A["abl<br/>带漏洞的签名 ABL"]
  B["efisp<br/>BDS.efi"]
  C["persist /efisp/<br/>boot.efi + 附属文件"]
  D["Android"]
  A -->|"GBL 加载 efisp"| B
  B -->|"读取 canoe.cfg"| C
  C -->|"投射策略"| D
```

- `abl` 保存签名、原厂且有意保持较旧的带 GBL 漏洞 ABL；
- `efisp` 不格式化，由操作员将 `BDS.efi` 原始写入；
- `persist/efisp` 是启动根目录，包含 `canoe.cfg`、已修补的 `boot.efi` 及其
  匹配的 `.gm2p` 和 `.tzmap` 附属文件。

BDS 读取启动根目录，从不写入存储。`boot.efi.gm2p` 是从匹配 `vbmeta` 派生的
120 字节 profile；`boot.efi.tzmap` 是从未修补 ABL 派生的 256 字节映射。原始
BDS 是启动链中唯一的 whole-partition image。

## 五种支持的场景

### 1. 电脑端首次安装

操作员使用 fastboot 刷入带漏洞的 ABL 和 BDS，进入 Super Fastboot，再运行交互式
Python 程序。电脑端只通过 `fastboot oem mass-storage:persist` 访问启动根目录。

```bash
fastboot flash abl <vulnerable>.img       # 当前 ABL 已修复时执行
fastboot flash efisp BDS.efi
./canoe
# Windows：canoe.cmd
```

问卷会等待匹配的原厂 `images/abl.img` 和 `images/vbmeta.img`，询问槽位与模式，
然后将选定世代提交到 `persist/efisp`。

### 2. 电脑端更新

使用同一电脑端流程构建并安装下一套匹配世代。当前三件套会连同匹配附属文件
降为 `boot_backup.efi`，并生成可选择的 `android-backup`。手动添加的启动项
会被保留。

### 3. KernelSU 模块安装

在已 Root 的设备上安装模块，按中英文首次安装问卷操作，选择 Mode 0、1 或 2。
Mode 1 会询问 Recovery vbmeta graft 以及是否修补 `vendor_boot` 命令行。模块
默认从设备分区派生，提交启动根目录，并执行所需的设备分区写入。

两个派生来源都可以独立切换到非空的提供文件：
`/data/local/tmp/canoe/abl.img` 与 `/data/local/tmp/canoe/vbmeta.img`。默认始终
使用对应设备分区；提供镜像绝不会作为刷写载荷。

### 4. KernelSU 更新或 OTA 后安装

安装 OTA 后、重启前，在模块 WebUI 中按 **Flash To Other Slot**。它为即将启动的
槽位派生并安装加载器，刷新附属文件，用该槽位标记活动行，并在需要时把漏洞
ABL 复制到那里。

如果跳过该操作，新槽位带有没有 GBL 漏洞的原厂 ABL。BDS 不会加载，设备会以
原厂状态启动且没有挂钩。不会变砖：返回另一个槽位启动，或按下操作后再次重启。
受管理的 Mode 2 profile 属于安装世代，只由该操作刷新，OTA 本身不会刷新。自动
的 OTA 后修补被有意推迟。

### 5. 锁定 Bootloader 的临时 root

Android 工具包中的设备端 shell 包装器从活动槽位提供临时 root：

```sh
su -c sh ./build.sh --mode 0
su -c sh ./build.sh --mode 1
```

它只接受 Mode 0 和 Mode 1，只改变启动根目录树，不写入分区。可选的
`--abl PATH` 和 `--vbmeta PATH` 只改变派生输入。易受攻击的 ABL 和
`BDS.efi` 到 `efisp` 的 `dd` 由操作员自行负责。

## 命令界面

Linux 与 Windows 使用同一电脑端入口（`canoe` 或 `canoe.cmd`）：

```text
canoe
canoe build [--abl IMG] [--vbmeta IMG]
canoe install [--boot-root PATH] --slot a|b [--mode 0|1|2] \
              [--vendor-boot IMG] [--allow-new-signer]
```

`canoe build` 默认使用 `images/abl.img` 与 `images/vbmeta.img`；提供值会在派生
前复制到这些路径。省略 `--boot-root` 时，`canoe install` 使用 BDS 导出；否则
使用已挂载的 `persist/efisp`。电脑端实现是 Python，不会调用 shell。

Mode 1 的 Recovery 准备使用独立 graft 工具：

```text
vbmetaport <official recovery vbmeta> <custom recovery.img> <output.img>
```

输出大小不得增加。`vendor_boot` 功能是固定偏移的命令行修改，不附带 boot-image
二进制。

## 签名限制

成功的 Mode 2 派生只能说明 `vbmeta` 已解析并带有签名和公钥 blob，不能说明密钥
属于 OEM；本工具无法证明这一点。自动保护仅检测公钥摘要是否相对于上一安装世代
发生变化。切换到或切换回 Custom ROM 时，变化是预期的；电脑端需要
`--allow-new-signer`，而明确提供设备端 `vbmeta` 即表示操作员作出该选择。

## 构建发布包

在 Linux 开发主机上构建：

```bash
make target_toolkit_linux
make target_toolkit_windows
make target_toolkit_android
make target_magisk_module
```

Linux 与 Android 工具包包含 `extractfv`、`patch_abl`、`mode2_profile` 和
`abl_tzmap`；Windows 工具包包含对应 `.exe`。Windows 还附带固定版本的
`fastboot.exe`、Ext4Windows 与 WinFsp。Ext4Windows 默认只读，安装导出磁盘时
必须使用：

```text
ext4windows.exe mount \\.\PhysicalDrive<N> Z: --rw
```

如果挂载失败，请运行 `ext4windows.exe --scan`，手动挂载卷，然后带必需槽位和
模式参数重新运行 `canoe.cmd install --boot-root <drive>:\efisp`。详见
[安装指南](wiki/docs/zh/install.md)、[`canoe.cfg` 契约](wiki/docs/zh/canoe-cfg.md)
和 [USB Mass Storage 指南](wiki/docs/zh/mass-storage.md)。

## 许可证与历史

项目采用 GPL-2.0-or-later。旧版 6.x 内容记录在 [`ARCHIVE.md`](ARCHIVE.md)；当前
发布界面仅包含上面的五种场景。
