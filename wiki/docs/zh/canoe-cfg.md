# canoe.cfg 配置契约

配置位于已挂载 efisp.fat 根目录的 canoe.cfg，BDS 路径为 \canoe.cfg。
image 与 BLS 路径以此 FAT 根目录为基准，不添加旧版 \efisp 前缀。
boot_a.efi/boot_b.efi 必须带各自匹配的 GM2P/TZ-map。不存在旧目录自动迁移或自动备份轮换。
普通菜单选择仅影响一次启动；显式保存默认项、Android 项模式或启动策略才写入配置，并保留已验证的 canoe.cfg.prev。

完整字段、边界与示例以[当前配置契约](../canoe-cfg.md)为准。

配置版本为 1。7.0.0-b6 使用显式启动策略，具体键值见上方规范。

7.0.0-b6 为每个已安装槽位管理一组三件套，包括加载器及其匹配的 GM2P 和 TZ-map 文件。

新增全局键 `show-booting yes|no`，缺省为 `yes`。CBM 和 BDS 的“隐藏 Booting…”选项共享此键。空的策略配置也可保存，不必创建 Android 启动项。保存默认项不修改模式；修改模式不改变默认项。启动时音量上或音量下打开启动菜单；在菜单中选择“Enter Super Fastboot”进入对应会话。

Menu 策略自动打开菜单且已保存的默认启动项可用时，标题显示 `Boot menu - Highlighted entry will boot in Xs.`（高亮项将在 X 秒后启动）。用任一音量键打开菜单或操作菜单会取消倒计时，标题显示 `Boot menu - Timeout is disabled.`（倒计时已禁用）。从子菜单返回不会重新计时；没有可用默认项时也不会倒计时。

默认倒计时为三秒，已明确保存的其他时长保持不变。启动根目录不存在或为空时，直接打开同一菜单，高亮临时的“Entering Super Fastboot”启动项并倒计时三秒。原有的“Enter Super Fastboot”操作始终保留；两者执行同一操作，临时项不会保存到配置。Power/Enter 选中该项；音量键取消倒计时并正常导航。不再使用独立的首次启动画面或额外按键等待窗口。文件系统读取失败仍单独报告，不视为空安装。
