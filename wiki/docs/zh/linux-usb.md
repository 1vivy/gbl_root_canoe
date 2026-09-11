# Linux USB 访问权限

Canoe Boot Manager 需要权限才能打开 CANOE-BDS 的受管理 USB 导出。
在使用 udev 和 systemd-logind 的 Linux 桌面上，一次性安装此专用
[uaccess 规则](https://github.com/1vivy/canoe-nusb-storage/blob/bbc4efa4dc9a47cf51319ac3a1b5d224b7e344f4/contrib/udev/70-canoe-managed-usb.rules)：

```sh
(
  rule_dir=$(mktemp -d) &&
  trap 'rm -rf "$rule_dir"' EXIT &&
  curl -fL https://raw.githubusercontent.com/1vivy/canoe-nusb-storage/bbc4efa4dc9a47cf51319ac3a1b5d224b7e344f4/contrib/udev/70-canoe-managed-usb.rules -o "$rule_dir/70-canoe-managed-usb.rules" &&
  sudo install -m 0644 "$rule_dir/70-canoe-managed-usb.rules" /etc/udev/rules.d/70-canoe-managed-usb.rules &&
  sudo udevadm control --reload-rules &&
  sudo udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=1209 --attr-match=idProduct=ca0f --settle
)
```

此命令下载 canoe-nusb-storage 已有的规则，安装并重新加载 udev，然后应用到
已连接的 `1209:ca0f` 导出。规则只向当前活动的本地用户授予访问权限，不会把
所有 USB 设备设为可写，不会挂载磁盘，也不会改变 Fastboot 的权限。保留
`70-canoe-managed-usb.rules` 文件名，确保它先于 `73-seat-late.rules` 应用访问列表。

返回应用并选择**检查连接**。应用可以复用浏览器已授权的设备，无需重新选择。
也可以拔插 USB 线。udev 的定向触发本身不会重置 USB，因此可能不会产生浏览器
连接事件。若浏览器尚未获得设备访问授权，请使用**选择 USB 设备**或**连接 USB 存储**。

Fastboot/Fastbootd 可能还需要发行版提供的 Android USB 规则。Fastboot 连接正常
不能证明 CANOE-BDS 的受管理存储也已获得权限。不要以 root 身份运行浏览器，
也不要把 USB 设备改为所有用户可写。此规则适用于活动的本地登录；SSH 会话和
无界面服务需要各自范围明确的访问策略。

参见[规则所属项目的安装说明](https://github.com/1vivy/canoe-nusb-storage/blob/bbc4efa4dc9a47cf51319ac3a1b5d224b7e344f4/contrib/udev/README.md)
与 [Chrome 的平台要求](https://developer.chrome.com/docs/capabilities/build-for-webusb#linux)。
