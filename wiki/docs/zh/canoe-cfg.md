# `canoe.cfg`——启动根目录契约

`canoe.cfg` 是 7.x 菜单状态的唯一来源，取代 6.x 保存在原始 `efisp` 分区尾部的三条 1 KiB 记录。

## 为什么迁移记录

`efisp` 分区只存放一项内容：以原始方式写入的 `BDS.efi`。6.x 借用镜像末尾之外的空间，分别在 `end - 3072`、`end - 2048` 和 `end - 1024` 保存首选模式、默认启动项和自定义启动项。这样会造成两个问题：

- 新 `BDS.efi` 的 `dd` 只写入镜像长度，旧记录可能比写入它的加载器存活更久，在某个版本选择的模式会静默作用于下一个版本。
- BDS 必须成为写入器；在一台距离 EDL 只差一次启动失败的设备上，每条写入路径都增加变砖风险。

7.x 对此做了反转。**BDS 从不写入存储。**它只读取 `canoe.cfg` 并渲染菜单。写入方严格只有一个：`tools/canoe-device/canoe_boot_entry.sh`。电脑端工具、KernelSU 模块和 OTA watcher 都调用这一份脚本。

写入器接口如下：

```text
sh canoe_boot_entry.sh set <boot_root> --id ID --title TITLE --image IMAGE \
  --role active|inactive|backup|other [--mode 0|1|2] [--default] \
  [--global-mode 0|1|2] [--timeout SECONDS] \
  [--devinfo-repair asneeded|never]
sh canoe_boot_entry.sh remove <boot_root> --id ID
sh canoe_boot_entry.sh show <boot_root>
```

`set` 是 UPSERT：它会原地创建或替换指定名称的启动项，同时原样保留其他所有启动项，包括手动添加的自定义 ROM 启动项和 OTA 添加的槽位启动项。`show` 不写入任何内容。本页仍是写入器输出文档的规范语法与路径限制参考。

电脑端与设备端调用方都能真正读写 `persist`；BDS 不能写入，这不是待补齐的功能。BDS 内置的 ext4 驱动从设计上就是只读的，固件也拒绝未知的 EFI 变量，因此这里不能改用标准的 `Boot####` 与 `BootOrder` 变量。

## 位置

| 查看方 | 路径 |
| --- | --- |
| Android / Recovery | `/persist/efisp/canoe.cfg`（或 `/mnt/vendor/persist/efisp/canoe.cfg`） |
| BDS，在 ext4 `persist` 卷上 | `\efisp\canoe.cfg` |

`\efisp` 是启动根目录；文件中的每个 `image` 路径都相对于它解析。

## 编码与限制

- 7-bit ASCII。除 `\r` 和 `\n` 外，`0x20..0x7e` 之外的字节会拒绝该行。
- 支持 `LF` 或 `CRLF`。
- 最多读取 8192 字节，超出部分忽略。
- 最多 24 个启动项，后续启动项会以日志标记丢弃。

词法分析器会去除行首空白，将 `#` 视为注释标记，忽略空行，并跳过过长行而不截断。每行由 `key`、一段空格或制表符和 `value` 组成；`value` 延续到行尾并去除末尾空白。

## 全局键

全局键必须出现在第一个 `entry` 之前。缩进没有语义；`entry` 之后的每个键都属于该启动项。只允许在文件作用域出现的 `timeout`、`default`、`generation` 和 `devinfo-repair` 若出现在启动项内，会被视为拒绝，而不会追溯应用到文件全局。

| 键 | 值 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `version` | `1` | —— | **必需。** 其他值会拒绝整个文件。 |
| `generation` | `0..4294967295` | `0` | 作者递增计数器，仅用于显示和诊断。 |
| `timeout` | `0..60` | `5` | 菜单等待启动 `default` 的秒数；`0` 立即启动。 |
| `default` | 启动项 id | 无 | 无人操作时启动的启动项。 |
| `mode` | `0`、`1`、`2` | `1` | 没有声明自身 `mode` 的启动项所使用的回退值。 |
| `devinfo-repair` | `asneeded`、`never` | `asneeded` | 是否允许受管理启动修复 `DeviceInfo`。 |

## 启动项块

`entry <id>` 打开一个启动项块。缩进没有语义。

| 键 | 值 | 默认值 | 含义 |
| --- | --- | --- | --- |
| `entry` | 1–31 个 `[A-Za-z0-9._-]` 字符的 id | —— | 打开启动项块；重复 id 会拒绝后一个块。 |
| `title` | 1–47 个可打印 ASCII 字符 | id | 菜单行文本。 |
| `image` | 最多 198 个字符的启动根目录相对路径 | —— | **必需。** 不得含 `.` 或 `..` 组件、连续分隔符或末尾分隔符；`/` 会折叠为 `\`。 |
| `mode` | `0`、`1`、`2` | 文件全局 `mode` | 启动该镜像时使用的策略。 |
| `role` | `active`、`inactive`、`backup`、`other` | `other` | 仅用于界面呈现；菜单会给启动项加后缀。 |

如果 `image` 不存在于卷中，启动项会被丢弃。这样配置在镜像消失后会退化为仍然可用的启动项，而不是显示无法启动的行。

## 每个启动项的模式

6.x 只有一个全局模式记录，因此可能将 Mode 2 用于没有自身 profile 的镜像。`.gm2p` KeyMint profile 与 `.tzmap` TrustZone map 都是按镜像派生的 sidecar，且分别绑定特定的 ABL 与 vbmeta。

7.x 将模式归属于拥有这些 sidecar 的启动项。全局 `mode` 只为没有声明自身模式的启动项提供回退。菜单仍提供仅限下一次启动的会话覆盖，它不会写入任何位置。

## role 与第三个启动项

A/B 设备有两个 ABL，再加上安装轮换出的上一代备份。三者都是普通启动项，`role` 只负责说明它们在界面中的区别：

```
Android (slot A)          (active)
Android (slot B)          (inactive)
Android (previous)        (backup)
```

BDS 不会自行推断槽位状态。哪个镜像处于活动状态是写入配置的调用方已经知道的事实。

## DeviceInfo 修复策略

Mode 1 或 Mode 2 的受管理启动需要底层 `DeviceInfo` 呈现为已解锁；投射层让 ABL 看到已锁定状态，而真实状态仍保持解锁。7.x 的策略如下：

- Mode 0 是无 hook 的直通模式，不读取也不写入任何内容。
- `devinfo-repair never` 会拒绝修复；若启动需要修复，会报告这一点并如实以 Mode 0 继续。
- `devinfo-repair asneeded` 只在观测状态不满足请求模式时修复。

无论采用哪种策略，都会在作出决定前记录观测状态：`SFB: MARK devinfo-repair observed-unlocked=<0|1> observed-critical=<0|1> required=<0|1> action=<none|repair|refused>`。

## 示例

```
# canoe.cfg - 由 canoe_boot_entry.sh 管理。允许手动编辑，但写入器会
# 重写整个文件并删除注释。
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

entry android-b
  title Android (slot B)
  image boot_b.efi
  mode 1
  role inactive

entry android-backup
  title Android (previous)
  image boot_backup.efi
  mode 0
  role backup
```

## 缺失或无法解析

没有 `canoe.cfg`、`version` 错误或文件没有可用启动项都不是错误。BDS 会先探测已知的受管理路径 `boot.efi` 与 `boot_backup.efi`，以 `Android` 和 `Android (previous)` 提供它们，再加入可移动介质或 ESP 上发现的内容。所有这些启动项使用内置默认模式，菜单会显示而不是无人值守启动。

**空启动根目录**——既没有 `canoe.cfg` 也没有 `boot.efi`——是首次运行信号。BDS 会说明这一点并直接交给 Super Fastboot；这是主机工具能够安装内容的通道。
