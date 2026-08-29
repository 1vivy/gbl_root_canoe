# `canoe.cfg`——启动根目录契约

`canoe.cfg` 是 7.x 的 BDS 菜单状态。BDS 只读取该文件；电脑端 Python
事务与设备端安装器负责写入。文件位于 `persist/efisp` 下，原始
`efisp` 分区只包含 `BDS.efi`。

## 位置与语法

| 查看方 | 路径 |
| --- | --- |
| Android 或 Recovery | `/mnt/vendor/persist/efisp/canoe.cfg` 或 `/persist/efisp/canoe.cfg` |
| `persist` 卷上的 BDS | `\efisp\canoe.cfg` |

每个 `image` 路径都相对于启动根目录。文件使用 7-bit 可打印 ASCII，支持
`LF` 或 `CRLF`，最多读取 8192 字节，最多接受 24 个启动项。行首空白会被
忽略，`#` 开始注释，空行会被忽略，过长的行会跳过而不会截断。

每行由键、连续的一段空格或制表符和行尾值组成。`entry <id>` 打开启动项块。
ID 为 1–31 个 `[A-Za-z0-9._-]` 字符；标题为 1–47 个可打印 ASCII 字符。
`image` 路径必需，最多 198 个字符，不得包含 `.` 或 `..` 组件、连续分隔符
或结尾分隔符；`/` 会折叠为 `\`。

## 全局键

全局键必须出现在第一个 `entry` 之前：

| 键 | 值 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `version` | `1` | 必需 | 配置格式版本 |
| `generation` | `0..4294967295` | `0` | 递增的安装世代编号 |
| `timeout` | `0..60` | `5` | 无操作时启动默认项前等待的秒数 |
| `default` | 启动项 ID | 无 | 无人操作时启动的启动项 |
| `mode` | `0`、`1`、`2` | `1` | 没有自身模式的启动项的回退模式 |
| `devinfo-repair` | `asneeded`、`never` | `asneeded` | 是否允许受管理启动修复 `DeviceInfo` |

启动项块内允许 `title`、`image`、`options`、`mode` 与 `role`。启动项自身的
`mode` 优先于全局回退值。全局键出现在启动项内会被拒绝，不会追溯应用。

`options` 是以 UEFI LoadOptions 形式交给镜像的命令行，最多 383 个字符，
逐字节原样传递：它不是路径，因此 `/` 不会折叠为 `\`，看起来像路径的值也
保持原样。空的 `options` 会被记为拒绝行，而不是静默忽略。

这使得一个启动项可以承载 BDS 自身并不解析的载荷。`image` 指向启动根目录
`tools` 目录中的加载器，`options` 指明它应当启动的载荷：

```text
entry mu
  title Mu-Silicium
  image tools/FdLoader.efi
  options \mu\SM8850.fd 0x9FC00000 0x00300000

entry android-usb
  title Android from images
  image tools/AbootLoader.efi
  options --boot \img\boot.img --vendor-boot \img\vendor_boot.img
```

`FdLoader.efi` 接受 FD 镜像路径、十六进制加载基址与十六进制窗口大小，
链式启动原始的 Mu-Silicium 或 Project-Aloha 固件描述符。`AbootLoader.efi`
接受 `--boot` 与 `--vendor-boot`，可选 `--init-boot`、`--dtb-index` 与
`--cmdline`，启动 header 版本为 3 或 4 的 Android boot 镜像。两者的路径都
相对于加载器自身所在的卷。

## 两个受管理启动项

每次安装只会写入以下受管理启动项，不会写入其他受管理启动项：

| 行 | ID | 标题 | 镜像 | role | 写入时机 |
| --- | --- | --- | --- | --- | --- |
| 活动 | `android-a` 或 `android-b` | `Android (slot A)` 或 `Android (slot B)` | `boot.efi` | `active` | 每次安装；始终带 `default` |
| 备份 | `android-backup` | `Android (previous)` | `boot_backup.efi` | `backup` | `boot_backup.efi` 非空时；为空时删除 |

手动添加的启动项会原样保留。镜像不存在时，写入器不会自行创建或压缩该行；
BDS 会等到镜像出现后才能启动它。对旧的 `boot_a.efi` 与 `boot_b.efi` 行的
显式迁移是唯一例外：删除这些加载器、对应附属文件以及
`android-a`/`android-b` 行，并报告迁移结果。

活动 ID 与标题记录安装器标记的 GPT 活动槽位。`active` role 不是仅用于呈现：
若它与 GPT 活动槽位不一致，BDS 会标记 `SlotMismatch`；当该行是配置的默认项
时，BDS 会禁止无人值守启动并强制显示菜单。用正确槽位重新安装即可修复标记。

`role backup` 标识上一世代，也参与 BDS 的菜单处理。备份行是受管理行，因为
其镜像 `boot_backup.efi` 是 BDS 认识的路径。命名为 `boot_a.efi` 或 `boot_b.efi`
的行不是受管理槽位行，其配置模式不会生效。

## A/B 世代生命周期

首次安装时，所选槽位成为活动行，`boot.efi` 是唯一的世代。更新时，先将当前
三件套移动为 `boot_backup.efi`；如果匹配来源缺失，则删除相应附属文件；随后
将新三件套设为 `boot.efi`。因此备份行同时承载上一槽位的加载器和上一世代。

OTA 后，在重启前按下模块的 **Flash To Other Slot**。该操作为即将启动的槽位
派生加载器，并用该槽位标记新的活动行。若跳过该操作，新槽位没有受管理加载器
并会以原厂状态启动；任何配置行都不能让原厂 ABL 加载 BDS。

## 附属文件与模式

BDS 只会读取受管理路径 `boot.efi` 与 `boot_backup.efi` 的附属文件。这些路径的
`.gm2p` 是 120 字节 KeyMint profile，`.tzmap` 是 256 字节 TrustZone 接口映射，
两者都必须属于对应的加载器。按镜像保存的附属文件并非每一行都有效：手动添加
的行或 `boot_a.efi`/`boot_b.efi` 行属于直通行，BDS 永远不会读取它们的附属文件。

Mode 0 是不启用 hook 的直通模式。Mode 1 投射锁定的 DeviceInfo 视图并启用普通
受管理 hook。Mode 2 还使用匹配的 profile。菜单模式只是下一次启动的会话覆盖，
不会写入文件。启动项自身的模式优先，全局 `mode` 只作为回退值。

成功的 Mode 2 派生只能说明 `vbmeta` 已解析并带有签名和公钥 blob，不能证明
该密钥属于 OEM；本工具无法证明这一点。唯一的自动保护是检测公钥摘要是否相对
上一安装世代发生变化。从 Custom ROM 切换过去或切换回来时，签名变化是预期的，
并需要操作员明确允许所提供的固件。

## DeviceInfo 修复

Mode 1 或 Mode 2 启动在观测状态不满足请求模式时可以修复 `DeviceInfo`。
`devinfo-repair asneeded` 允许修复；`devinfo-repair never` 拒绝修复并如实
以 Mode 0 继续。Mode 0 不读取也不写入 `DeviceInfo`。启动日志会在作出决定前
记录观测状态与采取的动作。

## 示例

```text
version 1
generation 4
timeout 5
default android-a
mode 1
devinfo-repair asneeded

entry android-a
  title Android (slot A)
  image boot.efi
  mode 1
  role active

entry android-backup
  title Android (previous)
  image boot_backup.efi
  mode 0
  role backup
```

## 缺失或无效配置

没有配置文件、`version` 无效或文件没有可用启动项，本身都不是错误。BDS 会探测
已知受管理路径 `boot.efi` 与 `boot_backup.efi`，在缺少配置时显示菜单而不无人值守
启动。既没有 `canoe.cfg` 也没有 `boot.efi` 的空启动根目录表示首次运行，BDS 会
进入 Super Fastboot，操作员可在其中安装启动链。
