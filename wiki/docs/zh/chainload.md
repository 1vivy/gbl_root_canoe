# 链式启动第三方 UEFI 栈

BDS 是链式启动选择器，不是引导管理器。它枚举候选镜像并启动其中一个，仅此而已：
`LoadImage` 然后 `StartImage`，并把该行的 `options` 逐字节交过去。它不枚举 USB，
不为其他操作系统实现引导管理器，也不携带任何载荷格式的加载器。

因此，想被链式启动的东西，契约很短：**它必须是一个 UEFI 应用程序**。

```text
entry mu
  title Mu-Silicium (infiniti)
  image mu/place.efi
  options \efisp\mu\Mu-infiniti.bin
```

`image` 是那个 PE。`options` 是该 PE 自己的参数语法所需要的内容——BDS 既不解析
也不校验。语法见 [`canoe.cfg` 契约](./canoe-cfg.md)，特别是其中解释为什么
`options` 中的路径需要带 `\efisp`、而 `image` 不需要。

上例中的 `place.efi` 位于 `canoe-uefi-handoff` 附属项目。它的参数只有一个路径：
它所进入的 blob 是 Project Mu 的 boot shim 加固件描述符，而 shim 的头部已经携带了
加载基址与窗口大小，因此没有任何十六进制数需要人手抄写。早先的设计接受
`<路径> <基址> <大小>`，四个设备周期中有两个耗在把这些数字和它们的路径前缀弄对上，
这正是现存设计不再索取它们的原因。

## 为什么 BDS 不再提供载荷加载器

它曾经提供过两个：一个把原始固件描述符复制到固定物理地址并跳转，另一个解析
Android boot 镜像并组装内核交接状态。两者都已移除，原因值得记录。

Project Mu 移植产出的固件描述符是按固定基址链接的。把它放到该地址意味着向运行中的
UEFI 分配器请求这个精确地址，而分配器有权拒绝——在一加 15 上实测，它确实拒绝：

```text
FdLoader: reserve 0xC6900000 (3145728 bytes) failed (Not Found)
```

设备树在该范围上没有任何 carveout，内核也把它报告为普通 `System RAM`，因此拒绝来自
固件自身的分配器占用了那里的页。强行覆盖预留并照样复制，会在 `ExitBootServices`
之前写入运行中固件可能仍在使用的内存，且无法输出任何诊断信息。

那次复制的正确位置是在 `ExitBootServices` **之后**——那时已不存在分配器——而这正是
Project Mu 的 boot shim 所做的事，也是上游为何要携带一个 shim。这段代码应当与它硬编码
了链接地址的那个描述符放在一起，而不是放在一个本不该知道“加载基址”是什么的选择器里。

得出同一结论的参考实现：高通自家的 `abl2esp` 启动另一个镜像时，只用
`LoadImage`/`StartImage` 打开 `\EFI\BOOT\BOOTAA64.EFI`；GRUB 的 arm64 直接加载器
从不请求固定基址——它接受分配器给出的任意地址，并在其内部对齐。

## 实际含义

| 你想启动的 | 应当以什么形式提供 | BDS 做什么 |
| --- | --- | --- |
| Project Mu / Aloha 固件描述符 | 一个在 `ExitBootServices` 之后放置它的 UEFI 应用 | 启动该 PE |
| Linux | GRUB，或任何带 EFI stub 的内核 | 启动该 PE |
| 另一个 bootloader，包括自行编译的 ABL | 它的 UEFI 应用形式 | 启动该 PE |
| Android | 受管理的 `boot.efi` | 启动该 PE，并启用模式 hook |

指向自行编译 ABL 的 `canoe.cfg` 启动项是完全正当的：像 `abl2esp` 这类项目在被包装进
`abl` 分区之前，其内层产物就是一个普通的 UEFI 应用。

完整分析——参考实现、四条候选路径、以及各上游项目需要做哪些调整——见配套项目
`canoe-uefi-handoff`。

## 不是受管理的启动

不属于那四条受管理 Android 路径的启动项是直通的：`efisp` 递归保护与 Mode 1/2 策略
hook 不会围绕它启用。这是正确的，因为此后机器归载荷所有，那些 hook 已无对象可管。
