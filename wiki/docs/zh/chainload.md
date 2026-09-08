# EFI 与 Linux 链式加载

BDS 通过 LoadImage/StartImage 启动普通 EFI 应用；BLS Linux 启动项可为 EFI-stub
内核提供 initrd 与设备树。Mode 1/2 hook 仅用于设备启动根目录中的受管理 Android 加载器。

镜像放在已挂载 efisp.fat 根目录或普通 FAT 介质中。配置与 BLS 的镜像路径从卷根开始，
不添加旧版 efisp 前缀。options 原样传给载荷。CLI 使用 --artifact 目标路径=源文件，
先发布镜像，再发布启动项；应用负责额外的快照、读回与恢复。

设备启动根目录上的 BLS 行可作为默认项，外部可移动介质不能。普通选择为一次性启动，
显式 Save as default 才保存。参见[完整说明](../chainload.md)及[命令](../commands.md)。

## 为什么 BDS 不再提供载荷加载器

它曾经提供过两个：一个把原始固件描述符复制到固定物理地址并跳转，另一个解析 Android
boot 镜像并组装内核交接状态。两者都已移除，原因值得记录。

Project Mu 移植产出的固件描述符是按固定基址链接的。把它放到该地址意味着向运行中的
UEFI 分配器请求这个精确地址，而分配器有权拒绝——在一加 15 上实测，它确实拒绝：

```text
FdLoader: reserve 0xC6900000 (3145728 bytes) failed (Not Found)
```

设备树在该范围上没有任何 carveout，内核也把它报告为普通 `System RAM`，因此拒绝来自
固件自身的分配器占用了那里的页。强行覆盖预留并照样复制，会在 `ExitBootServices` 之前
写入运行中固件可能仍在使用的内存，且无法输出任何诊断信息。

那次复制的正确位置是在 `ExitBootServices` **之后**——那时已不存在分配器——而这正是
Project Mu 的 boot shim 所做的事，也是上游为何要携带一个 shim。这段代码应当与它硬编码
了链接地址的那个描述符放在一起，而不是放在一个本不该知道“加载基址”是什么的选择器里。

得出同一结论的参考实现：高通自家的 `abl2esp` 启动另一个镜像时，只用
`LoadImage`/`StartImage` 打开 `\EFI\BOOT\BOOTAA64.EFI`；GRUB 的 arm64 直接加载器从不
请求固定基址——它接受分配器给出的任意地址，并在其内部对齐。

## 实际含义

| 你想启动的 | 应当以什么形式提供 | BDS 做什么 |
| --- | --- | --- |
| Project Mu / Aloha 固件描述符 | 一个在 `ExitBootServices` 之后放置它的 UEFI 应用 | 启动该 PE |
| Linux | 普通启动项或 BLS `linux` 启动项中的 GRUB，或 EFI stub 内核 | 启动该 PE；BLS 会发布 initrd/DTB |
| 另一个 bootloader，包括自行编译的 ABL | 普通启动项或 BLS `efi` 启动项中的 UEFI 应用 | 启动该 PE |
| Android | 受管理的 `boot_a.efi`、`boot_b.efi` 或 `boot_backup.efi` 三件套 | 启动该 PE，并启用模式 hook |

指向自行编译 ABL 的 `canoe.cfg` 启动项是完全正当的：像 `abl2esp` 这类项目在被包装
进 `abl` 分区之前，其内层产物就是一个普通的 UEFI 应用。

完整分析——参考实现、四条候选路径、以及各上游项目需要做哪些调整——见配套项目
`canoe-uefi-handoff`。

## 不是受管理的启动

不属于当前启动根目录受管理路径的启动项是直通的：`efisp` 递归保护与 Mode 1/2 策略
hook 不会围绕它启用。所有 BLS 行和可移动介质行都属于此类。这是正确的，因为此后
机器归载荷所有，那些 hook 已无对象可管。
