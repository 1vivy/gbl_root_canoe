# canoe.cfg 配置契约

配置位于已挂载 efisp.fat 根目录的 canoe.cfg，BDS 路径为 \canoe.cfg。
image 与 BLS 路径以此 FAT 根目录为基准，不添加旧版 \efisp 前缀。
boot_a.efi/boot_b.efi 必须带各自匹配的 GM2P/TZ-map。不存在旧目录自动迁移或自动备份轮换。
普通菜单选择仅影响一次启动；显式保存默认项、Android 项模式或启动策略才写入配置，并保留已验证的 canoe.cfg.prev。

完整字段、边界与示例以[当前配置契约](../canoe-cfg.md)为准。

配置版本为 1。7.0.0-b5 使用显式启动策略，具体键值见上方规范。

7.0.0-b5 为每个已安装槽位管理一组三件套，包括加载器及其匹配的 GM2P 和 TZ-map 文件。

新增全局键 `show-booting yes|no`，缺省为 `yes`。CBM 和 BDS 的“隐藏 Booting…”选项共享此键。空的策略配置也可保存，不必创建 Android 启动项。保存默认项不修改模式；修改模式不改变默认项。启动时音量上进入 Super Fastboot，音量下进入同一启动菜单。
