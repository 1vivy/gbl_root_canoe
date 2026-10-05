# 使用 Canoe

Overview 显示设备状态和已保存操作；Deploy 用于准备、审阅和写入；Entries 管理 EFI/BLS；Settings 提供策略、语言和卸载；Diagnostics 提供观察信息及记录。

Prepare Mode 1 boot images 直接进入镜像准备，默认使用活动槽验证资料并保留已安装模式，不更新加载器、BDS、工具或启动项模式。未完成操作可重试或审阅 Revert，成功收据仍可查看。

遇到问题且需要日志时，回到 BDS 菜单并选择 **USB Mass Storage → Export logfs**，再从主机复制该可移动磁盘；这取代旧的 Android/Recovery 手动挂载与拉取命令。

普通 BDS 菜单选择只影响本次启动。显式 Save as default 才保存偏好，并保留已验证的 canoe.cfg.prev 作为缺失/损坏配置的回退。

参见[完整说明](../usage.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。

已有启动项时，启动窗口内只有音量上会打开 CANOE-BDS 启动菜单；在菜单中选择“Enter Super Fastboot”进入对应会话。启动窗口默认为 1200 ms，限制在 500–5000 ms，读取旧配置时会将超出范围的值钳制到此范围。启动根目录不存在或为空时，直接打开同一菜单，高亮临时的“Entering Super Fastboot”项并倒计时三秒；原有“Enter Super Fastboot”操作始终保留。Power/Enter 选中该项，音量键取消倒计时并正常导航。不再使用独立首次启动画面或额外按键窗口。Advanced 提供独立的默认项、Android 项模式及启动策略设置；Reboot 提供 Fastbootd、Bootloader、Recovery、System。隐藏 Booting… 与 CBM 共享 `show-booting` 配置。

Menu 策略自动打开菜单且默认启动项可用时，**Boot menu** 标题下方的独立一行显示高亮项启动倒计时。按键操作或用音量上打开菜单后，该行显示 **Timeout is disabled.**；从子菜单返回不会重新开始。默认倒计时为三秒，`menu-timeout 0` 可关闭它，已明确保存的其他时长保持不变。

菜单采用居中的无边框文本区域，保留两侧相同的内边距和选择标记栏。明亮的 **Boot menu** 标题和独立倒计时行居中显示，其下的构建版本信息使用较暗颜色。当前项说明和操作名称左对齐，各组之间用八个短横线分隔。选中行的高亮覆盖整个可用行宽，底部操作说明以较暗颜色居中显示。较长的标题和说明在区域内换行，启动项名称保持单行，较长菜单可滚动。BDS 子菜单和 EFI 文件浏览器共用同一套绘制及导航逻辑；独立的 Android EFI 工具保留各自界面。

## 一次性启动（Boot once）

若只想为下一次 BDS 启动选择目标，打开 **Advanced → Arm boot once** 并选择一个启动项。Super Fastboot 提供以下主机命令：

```text
fastboot oem boot-once <selector> [<target>]
fastboot oem boot-once:<selector>
fastboot oem boot-once-clear
fastboot oem boot-direct <selector> [<target>]
```

`boot-once` 只写入记录并应答，不复位也不启动。`boot-once:<selector>` 是既有的冒号形式，始终写入不带标签的记录。`boot-once-clear` 清除记录。`boot-direct` 立即解析并启动该项，不复位也不写入一次性记录：它先应答主机，再把控制权交给所启动的项。

两个选择命令的 `<selector>` 都是可解析的 `canoe.cfg` 启动项 id、`bls:<stem>`，或 `fastboot`（常驻 Super Fastboot 操作）。只有 `boot-direct` 还接受字面量 `default`，它选择 `canoe.cfg` 的 `default` 所解析到的启动项；未配置默认项时命令失败，绝不回退到当前高亮的那一行。`boot-once default` 会被拒绝，因为存储的记录必须指向稳定目标。

可选 `<target>` 只能是 `recovery`、`fastbootd` 或 `menu`。前两者向 bootloader control block 写入标准 Android 命令；`menu` 写入私有的一次性命令 `surfacer-menu`，Surfacer 会在显示固件菜单前清除它，并向 GBL 暴露为普通启动，以免菜单选择随后被旧的 recovery 意图覆盖。仅当解析到的行是受管理的 Android ABL 启动项时才接受 target；否则命令失败且不写入任何内容。不带 target 时，`boot-direct` 完全不写 BCB，而 `boot-once` 只写 Canoe 记录，不写目标命令。

该记录只占用 misc LBA 0 处 32 字节的 Android BCB 命令字段，为 NUL 结尾的 ASCII。不加标签的语法是 `canoe-once:<selector>`：11 字节的 `canoe-once:` 前缀和结尾 NUL 共同留下 20 字节的 selector 空间。加标签的记录是 `canoe-once:<selector>+<target>`，其 selector 预算为 `32 - 11 - 1 - len(tag) - 1` 字节：`recovery` 为 11 字节，`fastbootd` 为 10 字节，`menu` 为 15 字节。selector 使用 `[A-Za-z0-9._:-]`；超长会在任何写入之前被拒绝，绝不截断。命令字段之后的字节保持不变。

BDS 在解析或启动目标之前先清除并刷新该记录；因此清除失败会回退到正常启动策略，而不会冒着启动循环的风险。加标签的记录若指向的行已无法解析，或该行不是受管理的 Android ABL 启动项，则走既有的 **Boot-once target unavailable** 提示路径，不启动任何内容，并继续采用正常启动策略。如果加标签的记录已被消费、目标命令已写入，但启动随后失败，该命令会留在 `misc` 中，因此下一次启动仍会遵循该目标；菜单显示 **Reboot target command pending**，而不会声称已采用正常启动策略。两种结果共用一个保留的提示行，因此只会显示其中一个提示。

一次性启动不复位手机，也不写入 `canoe.cfg`。它在下一次 BDS 启动时被消费，即使所选子项失败或返回也如此；无法再解析的启动项会打开正常菜单并给出提示，而不是启动另一行。

### BCB 命令组合

Super Fastboot 将 Android BCB 命令字段公开为有界的比较后写入操作：

```text
fastboot oem bcb-command get
fastboot oem bcb-command set <new>
fastboot oem bcb-command replace <expected> <new>
fastboot oem bcb-command clear <expected>
```

`set` 要求字段为空；`replace` 与 `clear` 要求当前逻辑命令精确等于
`<expected>`，不匹配时绝不写入。token 必须是 1–31 字节的可打印非空格
ASCII。每次修改只重写 `[0,32)`，保留 `misc` 的其余内容，并在刷新后精确
读回验证；这不是任意偏移或整个 BCB 的编辑器。

Surfacer RAM 包可用以下组合：

```text
fastboot oem bcb-command set surfacer-menu
fastboot boot phone-<build>.efisp
```

若 RAM 启动在 Surfacer 消费该命令前失败，先用 `get` 检查，再用
`clear surfacer-menu` 仅移除该值。`boot-direct`/`boot-once` 的 recovery、
fastbootd 与 menu 便捷 target 在内部共用同一命令字段存储边界。

### 只读 pstore

SFB 可以只读检查活动设备树中唯一的 `ramoops` 或 `qcom,ramoops` 区域：

```text
fastboot oem pstore
fastboot oem pstore info
fastboot oem pstore console
fastboot oem pstore pmsg
```

前两种形式报告解析出的物理区域及 console/pmsg 几何信息。记录命令验证
持久 RAM 头，并通过有界的 `INFO` 包最多输出最新 48 KiB；不可打印字节显示
为 `\xNN`。发现过程会拒绝歧义节点、错误几何、内存映射中不可读的范围及
ECC 布局。不存在清除、擦除或写入形式。

### 从已 root 的 Android shell

模块以命令形式提供同一操作，因此无需进入菜单或连接主机即可指定目标：

```sh
/data/adb/modules/fake_bl_efisp/bin/canoe-manager boot-once show
/data/adb/modules/fake_bl_efisp/bin/canoe-manager boot-once arm fastboot --reboot
/data/adb/modules/fake_bl_efisp/bin/canoe-manager boot-once clear
```

`--reboot` 在记录写入后执行一次普通重启。请用这种方式重启，而不要用 `reboot recovery` 或 `reboot bootloader`：两者都会向同一 BCB 字段写入各自的命令，静默丢弃已写入的记录。同理，当 `boot-recovery` 之类的厂商命令已在等待时，写入会被拒绝；传入 `--replace` 可有意取消它。`clear` 只移除 Canoe 记录，不触碰厂商命令。

## Super Fastboot 界面

BDS 等待主机时会显示：

- **Stay in Fastboot** —— 无操作；它只重绘，并且是初始光标行；
- **Reboot to Recovery**；
- **Power Off**；以及
- **Restart**。

从主机侧，支持的重启目标与直接启动命令为：

```bash
fastboot reboot              # Android
fastboot reboot recovery     # recovery
fastboot reboot bootloader   # bootloader target
fastboot reboot fastboot     # userspace Fastbootd
fastboot oem boot-direct <selector> [<target>]
```

`boot-direct` 是无需复位即可离开 Super Fastboot 进入所选启动项的方式。它先解析启动项；提供 target 时，先写入并刷新标准 BCB 命令，应答主机，然后把控制权交给所启动的项；不带 target 时不写 BCB。它从不写入一次性启动记录。

如果交接失败，则刻意不启动该项：设备留在 Super Fastboot，会话尝试恢复链路。由于应答早于交接，主机仍会收到 `OKAY`，因此可能出现成功响应后没有启动。此时设备日志记录 `SFB: MARK boot-once handoff=failed` 与 `SFB: MARK boot-direct-release status=...`。

其他重启目标会失败。Recovery 与 Fastbootd 会准备 `misc` 中的 bootloader control block、刷新它，并通过普通启动路径重启。固件与所选启动链必须支持该目标；这不是 stock ABL 的逃生通道。System 会在重启前清除已知的 recovery/Fastbootd 命令。
