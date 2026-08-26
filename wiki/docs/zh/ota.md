# 小米与一加系统更新安全警告

## 7.x OTA watcher

已安装的设备端模块现在包含后台 `service.sh` watcher。它使用 `inotifyd` 监视 `abl_a` 与 `abl_b` 设备节点，并根据安装时记录的 SHA-256 摘要检查候选变化；没有 `inotifyd` 时会改用慢速轮询。OTA 通常会更改非当前槽位；ABL 真正发生变化后，watcher 会重新派生该槽位的配对，并以正确的 role 将新的启动项加入 `canoe.cfg`。

watcher 会保留当前正在启动的启动项。之前能正常工作的启动项绝不会被删除。OTA 后不需要每次重新打开 WebUI 并刷写。如果要更改模式，WebUI 选择器仍然可用，但它修改的是指定名称的 `canoe.cfg` 启动项，而不是分区记录。

## 通用 SCM 保护（所有模式）

Mode 0/1/2 在启动和 OTA 刷新期间都会尽力抑制 TrustZone 熔断和 anti-rollback SCM 请求，但这只能阻止**进一步推进**：无法让已经熔断的 fuse 复原，也无法降低已经升高的 rollback floor。如果 SCM 协议不存在，启动仍会继续，并通过 `hooks-armed ... scm=0` 标记记录保护不可用。

## 小米

目前小米在 **300** 修复了 GBL 漏洞，但是截止 **306**，XBL 仍有启动旧版 ABL 来间接加载 `efisp` 的能力。

**OTA 方式：**

watcher 会处理非当前槽位的 ABL 变化，并将匹配的新启动项加入 `canoe.cfg`；它不会删除当前正在启动的启动项。每次 OTA 后不需要手动刷写。只要保持模块安装，让 watcher 在后台运行即可；只有想要更改某个启动项的模式时，才需要使用 WebUI。修补后的 loader 与 `.gm2p` 附属文件仍必须描述同一套原厂固件配对。

**严重风险：**

如果 ABL 的 AVB 版本发生变化，该方法可能导致**黑砖**。watcher 无法让本身不兼容的厂商 ABL 变得安全。

**建议：**

- 使用**雹**冻结系统更新。
- **非必要 / 主力机器不要更新。**
- 如果一定更新，请确保检查 ABL 的 anti-rollback 版本，或等待前人测试过。

**版本信息：**

- 修复加载 `efisp` 的 ABL 版本：OS3.0.300
- 当前测试最高使用模块成功更新版本：3.0.306

## 一加

较新的版本修复了 loader 路径，因此应让旧版漏洞 ABL 留在 `abl` 分区；鉴于之前的熔断事件，仍建议使用雹冻结系统更新。

**警告：**

- **非必要 / 主力机器不要更新。**
- 如果一定更新，请确保检查 ABL 的 anti-rollback 版本，或等待前人测试过，或确认新版本仍未修复 GBL 漏洞。
- OTA 后模块 watcher 可以添加非当前槽位的启动项；它不会删除当前正在启动的启动项。

**版本信息：**

- `16.0.5.7xx` 及更低版本带漏洞；更新的版本已修复，因此 `abl` 分区要保留一个带漏洞的 ABL，让修补后的 loader 跟随你的固件。

## 关于未来熔断 ABL anti-rollback 版本

如果后续真的开始熔断 ABL anti-rollback 版本，建议直接放弃更新，或者只更新 **HLOS**。

### 提取 HLOS 方法

1. 解压 `payload.bin` 到 `images` 目录。
2. 使用以下脚本检查 `images` 目录下的文件是否包含 AVB0 头，如果包含则认为是 HLOS，否则认为是非 HLOS。

```python
#!/usr/bin/env python3
img_dir = "./images"
import os
for img in os.listdir(img_dir):
    with open(os.path.join(img_dir, img), "rb") as f:
        if b"AVB0" in f.read():
            print(f"{img} is an hlos image")
```

3. 使用 fastboot 刷写这些分区。

## 启动故障症状对照

| 症状 | 可能原因 | 恢复方式 |
|------|----------|----------|
| 厂商 logo 之后黑屏，没有 fastboot 界面；电脑端识别为 `QUSB_BULK_CID`（EDL 9008） | ABL 启动之前就已失败：当前槽位上的 ABL 无法运行——例如刷入了不属于本机的 ABL，或 Bootloader 切换到了模块从未配对过的槽位 | 通过授权 EDL 刷写与设备匹配的当前固件 |
| Bootloader 和 Recovery 都正常，但系统无法启动 | 启动链不匹配或只装了一半：`efisp` 里的 BDS 与 `persist` 里的 sidecar 来自不同世代 | 将整条启动链作为整体重装（重装模块或重新走工具包安装流程），确保 `BDS.efi`、`boot.efi`、`.gm2p`、`.tzmap` 同属一套 |
| 红屏 | 固件拒绝了 verified-boot 状态 | 进入 Recovery，刷新或移除启动链 |

## 关于模块

模块安装脚本与 WebUI 均支持中文和英文。
