# USB 大容量存储

BDS 可将 persist/efisp.fat 作为普通可移动 FAT 启动根目录导出。Windows 可以自动挂载它；仅导出外层 ext4 persist 时，Windows 不会看到其中的 FAT 文件。启动根目录中的路径不再添加 efisp 前缀。共有三个导出目标：`boot-root`、`persist` 与 `logfs`；BDS 的 USB 菜单只列出当前可解析的目标。

导出分两种模式。**受管模式**由应用驱动：设备以厂商类 `1209:ca0f` 枚举并携带接口级 WinUSB 描述符，操作系统的文件系统驱动不会接管它，因此应用是唯一写入者——无需挂载、弹出，也不必安装 ext4 驱动；受管驱动无法启动时导出直接失败，不会退回可被系统挂载的磁盘。应用按设备/容器身份绑定导出，保留无关文件，拒绝冲突，并在释放前刷新。**手动模式**以普通大容量存储 `1209:ca0e` 枚举，由系统挂载，此时你只是多个写入者之一，请勿同时用其他程序编辑同一启动根目录。离线 ext4 仅用于容器创建/删除；不需要 Windows ext4 驱动、Ext4Windows 或 WinFsp。

不要在 Android 使用 persist 时从主机同时写入它，也不要对已挂载 FAT 做原始扇区写入。

遇到问题且需要日志时，回到 CANOE-BDS 菜单并选择 **USB Mass Storage → Export logfs**。在主机上复制可移动磁盘里的文件，再安全弹出；提交问题时附上这些日志、设备型号、准确固件/地区、所选模式、问题说明与屏幕现象。无需在 Android 或 Recovery 中手动挂载，也无需 ADB pull。

参见[完整说明](../mass-storage.md)、[命令](../commands.md)、[旧版重装](../reinstall.md)与[数据格式化矩阵](../format-data.md)。
