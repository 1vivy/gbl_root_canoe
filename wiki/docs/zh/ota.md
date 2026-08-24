# 小米与一加系统更新安全警告

## 通用 SCM 保护（所有模式）

Mode 0/1/2 在启动和 OTA 刷新期间都会尽力抑制 TrustZone 熔断和 anti-rollback SCM 请求，但这只能阻止**进一步推进**：无法让已经熔断的 fuse 复原，也无法降低已经升高的 rollback floor。如果 SCM 协议不存在，启动仍会继续，并通过 `hooks-armed ... scm=0` 标记记录保护不可用。

## 小米

目前小米在 **300** 修复了 GBL 漏洞，但是截止 **306**，XBL 都有启动旧版 abl 来间接加载 EFISP 的能力。

**OTA 方式：**
模块不会自动刷写任何内容。OTA 包安装完成后、重启之前，需打开模块 WebUI（或使用手动工具包流程）显式刷新修补后的 ABL/profile/map 文件对与 `BDS.efi`。从匹配的原厂 vbmeta 重新生成 `boot.efi.gm2p`，并从未修补 ABL 重新生成可选的 `boot.efi.tzmap`。能否继续正常启动取决于新版本的 abl anti-rollback 版本是否保持不变。

**⚠️ 严重风险：**
一旦 abl avb 版本变化，该方法会**黑砖**。

**建议：**
- 使用**雹**冻结系统更新
- **非必要 / 主力机器不要更新**
- **非必要 / 主力机器不要更新**
- **非必要 / 主力机器不要更新**
- **非必要 / 主力机器不要更新**
- 如果一定更新，请确保检查 abl 的 anti-rollback 版本，或等待前人测试过

**版本信息：**
- 修复加载 efisp 的 abl 版本：OS3.0.300
- 当前测试最高使用模块成功更新版本：3.0.306


## 一加

目前暂未修复漏洞，但是鉴于之前熔断事件，依旧建议使用雹冻结系统更新。

**⚠️ 警告：**
- **非必要 / 主力机器不要更新**
- **非必要 / 主力机器不要更新**
- **非必要 / 主力机器不要更新**
- **非必要 / 主力机器不要更新**
- 如果一定更新，请确保检查 abl 的 anti-rollback 版本，或等待前人测试过，或确认新版本 GBL 漏洞未修复
- 也可使用模块 OTA

**版本信息：**
- 版本 **761** 修复


## 关于未来熔断 anti-rollback 版本

如果后续真的开始熔断 ABL anti-rollback 版本，建议直接放弃更新，或者只更新 **HLOS**。

### 提取 HLOS 方法

1. 解压 `payload.bin` 到 `images` 目录
2. 使用以下脚本检查 `images` 目录下的文件是否包含 AVB0 头，如果包含则认为是 HLOS，否则认为是非 HLOS

```python
#!/usr/bin/env python3
img_dir = "./images"
import os
for img in os.listdir(img_dir):
    with open(os.path.join(img_dir, img), "rb") as f:
        if b"AVB0" in f.read():
            print(f"{img} is an hlos image")
```

3. 使用 fastboot 刷写这些分区


## 关于模块

模块安装脚本与 WebUI 均支持中文和英文。
