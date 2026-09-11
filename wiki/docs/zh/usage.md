# 使用 Canoe

General/Overview 显示设备状态和已保存操作；Deploy 用于准备、审阅和写入；Boot entries 管理 EFI/BLS；Settings 提供策略、语言和卸载；Diagnostics 提供观察信息及记录。

Prepare Mode 1 boot images 直接进入镜像准备，默认使用活动槽验证资料并保留已安装模式，不更新加载器、BDS、工具或启动项模式。未完成操作可重试或审阅 Revert，成功收据仍可查看。

普通 BDS 菜单选择只影响本次启动。显式 Save as default 才保存偏好，并保留已验证的 canoe.cfg.prev 作为缺失/损坏配置的回退。

参见[完整说明](../usage.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。

已有启动项时，启动窗口内音量上或音量下打开 CANOE-BDS 启动菜单；在菜单中选择“Enter Super Fastboot”进入对应会话。启动根目录不存在或为空时，直接打开同一菜单，高亮临时的“Entering Super Fastboot”项并倒计时三秒；原有“Enter Super Fastboot”操作始终保留。Power/Enter 选中该项，音量键取消倒计时并正常导航。不再使用独立首次启动画面或额外按键窗口。Advanced 提供独立的默认项、Android 项模式及启动策略设置；Reboot 提供 Fastbootd、Bootloader、Recovery、System。隐藏 Booting… 与 CBM 共享 `show-booting` 配置。

Menu 策略自动打开菜单且默认启动项可用时，**Boot menu** 标题下方的独立一行显示高亮项启动倒计时。按键操作或用任一音量键打开菜单后，该行显示 **Timeout is disabled.**；从子菜单返回不会重新开始。默认倒计时为三秒，`menu-timeout 0` 可关闭它，已明确保存的其他时长保持不变。

菜单采用居中的无边框文本区域，保留两侧相同的内边距和选择标记栏。明亮的 **Boot menu** 标题和独立倒计时行居中显示，其下的构建版本信息使用较暗颜色。当前项说明和操作名称左对齐，各组之间用八个短横线分隔。选中行的高亮覆盖整个可用行宽，底部操作说明以较暗颜色居中显示。较长的标题和说明在区域内换行，启动项名称保持单行，较长菜单可滚动。BDS 子菜单和 EFI 文件浏览器共用同一套绘制及导航逻辑；独立的 Android EFI 工具保留各自界面。
