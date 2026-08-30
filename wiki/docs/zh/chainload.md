# 链式启动第三方 UEFI 栈

BDS 是只读的 UEFI 选择器。它会扫描保留卷上的已知 EFI 加载器与 BLS Type #1
启动项，并启动用户选择的项目。普通启动项仍执行 `LoadImage` 后接
`StartImage`，并逐字节交出该行的 `options`；BLS `linux` 行还会向 EFI stub
内核发布 initrd 与设备树。BDS 不是通用操作系统 boot manager，不解析 Android
boot 镜像或原始固件描述符，也不携带载荷加载器。

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

## BLS Type #1 启动项

BLS 为可启动文件提供第二套声明命名空间。每个
`loader/entries/<name>.conf` 文件对应一个 Type #1 启动项，并且必须包含且只包含
一个 `linux` 或 `efi` 键。`linux` 行指向 EFI stub 内核，可以再指定一个 `initrd`、
一个 `devicetree` 和命令行 `options`；`efi` 行指向普通 UEFI 应用，`options` 会
作为不透明的 LoadOptions 传递。为兼容发行版文件，未知的标准 BLS 键会保留；格式
错误、镜像缺失以及不支持的重复字段会被跳过。

boot manager 会用 SHA-256 校验启动项及其引用的全部文件，再进行暂存：

```bash
# 本地启动根目录：
sha256sum vmlinuz-canoe initramfs-canoe
canoe-bootmgr --boot-root /path/to/efisp bls stage \
  --name canoe-linux.conf --entry ./canoe-linux.conf \
  --artifact ./vmlinuz-canoe,vmlinuz-canoe,<KERNEL_SHA256> \
  --artifact ./initramfs-canoe,initramfs-canoe,<INITRD_SHA256>

# 直接 ext4 镜像或导出的块设备：
canoe-bootmgr --source <ext4-image-or-block-device> bls stage \
  --name canoe-linux.conf --entry ./canoe-linux.conf \
  --artifact ./vmlinuz-canoe,vmlinuz-canoe,<KERNEL_SHA256> \
  --artifact ./initramfs-canoe,initramfs-canoe,<INITRD_SHA256>
```

`--artifact` 格式为 `SOURCE,DESTINATION,SHA256`；每个目标都必须被解析后的
BLS 文件引用，摘要必须是 64 个十六进制字符。操作会在复制前和复制中校验源文件，
只有全部文件通过后才写入 `loader/entries/<name>.conf`，失败时回滚整个集合。
`--source` 与 `--ext4-image` 选择直接 ext4 后端；`--boot-root` 选择本地目录，
不能与它们组合。

### 两个路径命名空间

两套声明语法相对于不同的根目录命名路径：

| 声明 | 路径值 | persist ext4 解析结果 | FAT 解析结果 |
| --- | --- | --- | --- |
| `canoe.cfg` `image` | `mu/place.efi` | `\efisp\mu\place.efi` | `\mu\place.efi` |
| `canoe.cfg` `options` | 载荷拥有的不透明值 | 原样传递；载荷路径从 `\` 开始 | 原样传递；载荷路径从 `\` 开始 |
| BLS `linux`/`efi`/`initrd`/`devicetree` | 相对路径或带 `/` 的路径 | 加上 `\efisp\...` 前缀 | 卷根下的 `\...` |

因此，放在 `persist/efisp/loader/entries` 的 BLS 文件可以写
`linux /vmlinuz-canoe`，BDS 会打开 `\efisp\vmlinuz-canoe`。同一启动根目录中的
Canoe 行则写成不带前缀的 `image mu/place.efi`。普通行的 `options` 属于被启动的
载荷；如果载荷位于 persist，其中的路径必须包含 `\efisp`。

### 发现的 BLS 行不是无人值守默认项

BDS 将发现的 BLS 行追加在配置的 `canoe.cfg` 行之后。无人值守默认解析器只接受
`canoe.cfg` 中不可移动介质上的普通 EFI 行；发现的 BLS `efi` 或 `linux` 行不能
写入 `canoe.cfg default`。按住启动采样窗口内的音量上，进入菜单后选择 BLS 行并
按电源键。若要重复进行无人值守测试，可把小型 wrapper UEFI 应用作为普通
`canoe.cfg` 行，并将 wrapper 设为默认；wrapper 再选择或链式启动 BLS 文件。


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
| Linux | 普通启动项或 BLS `linux` 启动项中的 GRUB，或 EFI stub 内核 | 启动该 PE；BLS 会发布 initrd/DTB |
| 另一个 bootloader，包括自行编译的 ABL | 普通启动项或 BLS `efi` 启动项中的 UEFI 应用 | 启动该 PE |
| Android | 受管理的 `boot_a.efi`、`boot_b.efi` 或 `boot_backup.efi` 三件套 | 启动该 PE，并启用模式 hook |

指向自行编译 ABL 的 `canoe.cfg` 启动项是完全正当的：像 `abl2esp` 这类项目在被包装进
`abl` 分区之前，其内层产物就是一个普通的 UEFI 应用。

完整分析——参考实现、四条候选路径、以及各上游项目需要做哪些调整——见配套项目
`canoe-uefi-handoff`。

## 不是受管理的启动

不属于当前启动根目录受管理路径的启动项是直通的：`efisp` 递归保护与 Mode 1/2
策略 hook 不会围绕它启用。所有 BLS 行和可移动介质行都属于此类。这是正确的，
因为此后机器归载荷所有，那些 hook 已无对象可管。
