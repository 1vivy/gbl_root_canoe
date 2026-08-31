# `canoe.cfg`——启动根目录契约

`canoe.cfg` 是 7.x 的 BDS 菜单状态。7.0.0-b2 使用显式启动策略；新安装默认
为 Silent，写入器与 BDS 共享下文语法。BDS 只读取该文件；电脑端
`canoe-bootmgr` 事务与设备端模块负责写入。文件位于 `persist/efisp` 下，
原始 `efisp` 分区只包含 `BDS.efi`。

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
| `menu-mode` | `silent`、`menu` | 新安装为 `silent` | 启动策略 |
| `key-window` | `0..=10000` | `1200` | 音量键采样窗口（毫秒） |
| `menu-timeout` | `0..=300` | `5` | 菜单倒计时（秒），仅 Menu 模式生效 |
| `default` | 启动项 ID 或 `bls:<stem>` | 无 | 无人操作时启动的启动项 |
| `mode` | `0`、`1`、`2` | `1` | 没有自身模式的启动项的回退模式 |
| `devinfo-repair` | `asneeded`、`never` | `asneeded` | 是否允许受管理启动修复 `DeviceInfo` |

`key-window` 的有效范围含两端。`key-window 0` 表示不采样：Silent 模式立即
启动默认项，Menu 模式仍会打开菜单。Silent 模式在窗口内按住音量上键会打开
菜单并无限等待输入；音量下键进入现有 fastboot 路径；没有按键则立即启动默认项。
Menu 模式在窗口内检测到音量下键先进入 fastboot，随后总是打开菜单。菜单按
`menu-timeout` 秒倒计时后启动默认项；任意按键会取消倒计时并使菜单无限等待。
`menu-timeout 0` 表示永不自动启动。

写入器不会写出 `timeout`。BDS 仅将 b2 之前的 `timeout N` 行作为兼容别名接受，
等同于 `menu-mode menu` 加 `menu-timeout N`；它不是拒绝行，当前工具也不会写出它。

`default` 可以指向可解析的 `canoe.cfg` 启动项，也可以指向发现的 BLS Type #1
启动项 `bls:<stem>`。stem 是去掉 `.conf` 扩展名后的 ASCII 小写名称，必须匹配
`^[a-z0-9._-]{1,63}$`（例如 `loader/entries/pmOS.conf` 变为 `bls:pmos`）。
BLS 默认项保持直通：没有受管理附属文件、模式钩子或槽位语义。USB 上的 BLS
行不能作为无人值守默认项。默认项无法解析（包括未发现的 `bls:<stem>`）时，
BDS 打开菜单，显示现有拒绝/提示界面并等待输入；绝不会回退到其他启动项。
完整的写入器语法为：

```text
version 1
generation N
menu-mode silent|menu
key-window 1200
menu-timeout 5
default android-a          # or: default bls:pmos
mode 0|1|2
devinfo-repair asneeded|never
```


启动项块内允许 `title`、`image`、`options`、`mode` 与 `role`。启动项自身的
`mode` 优先于全局回退值。全局键出现在启动项内会被拒绝，不会追溯应用。

`options` 是以 UEFI LoadOptions 形式交给镜像的命令行，最多 383 个字符，
逐字节原样传递：它不是路径，因此 `/` 不会折叠为 `\`，看起来像路径的值也
保持原样。空的 `options` 会被记为拒绝行，而不是静默忽略。

这使得一个启动项可以承载 BDS 自身并不解析的载荷。BDS 是链式启动选择器：它启动一个
PE，并把 `options` 逐字节交过去，另一端的镜像完全拥有自己的参数语法。

```text
entry mu
  title Mu-Silicium
  image mu/place.efi
  options \efisp\mu\Mu-infiniti.bin

entry grub
  title GRUB
  image grub/grubaa64.efi
```

这两个镜像都不由本项目提供。BDS 不携带任何载荷加载器——原因以及第三方栈需要以何种
形式提供才能被启动，见[链式启动第三方 UEFI 栈](./chainload.md)。`place.efi` 来自
`canoe-uefi-handoff` 附属项目，只接受一个路径参数，因为它所进入的 blob 自身就描述了
加载基址与窗口大小。

### 两个路径命名空间

`image` 与被启动镜像自身 `options` 中的路径解析方式不同，混淆这一点是唯一会让
一个本来正确的启动项失败的错误。

`image` 由 BDS 解析，它会补上该卷的启动根目录。在 ext4 `persist` 分区上启动根目录
是 `\efisp`，因此 `image mu/place.efi` 实际加载 `\efisp\mu\place.efi`。
在 FAT 卷上启动根目录就是卷根，同样的值加载 `\mu\place.efi`。FAT 的位宽无关：
本机根本没有 FAT32 分区，格式化为 FAT16 的 U 盘也是普通的启动卷。

`options` 原样交出，被启动的镜像是相对它自身所在卷的**文件系统根**打开其中路径的，
它并不知道启动根目录的存在。因此放在 `persist/efisp/mu` 下的载荷必须写成
`\efisp\mu\...`；同一载荷放在 FAT U 盘上则写成 `\mu\...`。

这一点已在硬件上确认：`options` 写成 FAT 风格 `\mu\Mu-infiniti.fd` 的启动项报告
`Not Found`，而同一次启动中 `image mu/…` 则经启动根目录正确解析。

`options` 中的加载基址与窗口大小属于载荷，与 Canoe 无关。对 Mu-Silicium 构建，
它们是该设备 `Resources/Configs/<codename>.toml` 中 `[uefi_fd]` 的 `base` 与
`size`，与其 `MemoryMapLib.c` 中的 `UEFI_FD` 行一致；对 Project-Aloha 配置，
它们是 `StackBase` 与 `StackSize`。上面的取值来自一加 15（`infiniti`）。

## 受管理的 A/B 三件套

7.0.0-b2 写入每个已安装槽位的一组三件套：

| 槽位 | ID | `canoe-bootmgr` 写入的标题 | 镜像 | 附属文件 |
| --- | --- | --- | --- | --- |
| A | `android-a` | `Android A` | `boot_a.efi` | `boot_a.efi.gm2p`、`boot_a.efi.tzmap` |
| B | `android-b` | `Android B` | `boot_b.efi` | `boot_b.efi.gm2p`、`boot_b.efi.tzmap` |
| 上一世代 | `android-backup` | `Android (previous)` | `boot_backup.efi` | `boot_backup.efi.gm2p`、`boot_backup.efi.tzmap` |

`boot_a.efi` 与 `boot_b.efi` 是相互独立的受管理加载器。每个 `.gm2p`
附属文件必须正好 120 字节，`.tzmap` 必须正好 256 字节，并且必须属于
旁边的那个加载器；备份三件套使用相同大小。

写入器只会为拥有有效已安装三件套的槽位写入 `android-a` 或 `android-b`，
不会为另一个槽位创建空占位行。安装器标记的活动槽位为 `role active`，
另一个已安装槽位为 `role inactive`。只有 `boot_backup.efi` 与两个匹配的
附属文件构成有效上一世代时，才保留 `android-backup`。安装器刷新受管理
启动项，但不会自动创建 `default`；需要默认项时，使用 boot manager 的
default 命令显式设置。

手动添加的启动项会原样保留。镜像不存在时，写入器不会自行创建或压缩该行；
BDS 会等到镜像出现后才启动它。受管理 ID 保留给写入器使用；手写的
`android-a`、`android-b` 或 `android-backup` 会在下一次受管理安装时被替换。

`active` role 是功能性元数据，而不只是显示文字。BDS 会将 active 行对槽位
的声明与 GPT 活动槽位比较；不一致时标记 `SlotMismatch`，若该行是默认项，
则禁止无人值守启动。用正确的显式槽位重新安装即可修复标签。未知槽位会被
拒绝：使用 `--slot a` 或 `--slot b`；`--inactive` 还要求已知活动槽位元数据
以及明确的安全确认。

## A/B 世代生命周期与旧版迁移

安装会原地更新选定槽位。提交新三件套前，`canoe-bootmgr` 将该槽位已有的
三件套连同附属文件复制到 `boot_backup.efi`。因此 `boot_backup.efi` 是**最后
更新槽位的上一世代**，而不是永久的第三个槽位。如果选定槽位没有有效三件套，
就会移除备份三件套。`--both` 会在一个定义明确的事务中更新两个槽位；最终
备份仍是最后更新槽位的上一世代。

单一的 `boot.efi` 名称已从新安装中退役。迁移时，写入器接受完整的旧版
`boot.efi` 三件套；如果显式目标槽位没有有效三件套，就把它复制到该槽位，
随后删除旧文件。如果目标已有有效三件套，则直接删除完整的旧版三件套而不
复制；不完整的旧版集合会被移入隔离区。迁移后只保留按槽位命名的文件，以及
（存在时的）`boot_backup.efi`。旧 `boot.efi` 仅作为 BDS 对 b2 以前启动根
目录的兼容探测，不是当前受管理安装目标。

OTA 后保持系统运行，在重启前使用模块的 **Install to inactive slot** 操作。
该操作必须获得目标槽位元数据，只安装非活动槽位；它会拒绝把运行槽位重新标记
为目标或静默回退。若跳过该操作，新槽位没有受管理加载器，会以原厂状态启动；
任何配置行都不能让原厂 ABL 加载 BDS。

## 附属文件与模式

BDS 只会为受管理路径 `boot_a.efi`、`boot_b.efi` 与 `boot_backup.efi` 解释附属
文件。手动启动项的按镜像附属文件不会生效。可移动介质上恰好命名为受管理路径
的行仍是直通行；受管理策略只适用于设备启动根目录。

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
menu-mode silent
key-window 1200
menu-timeout 5
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
