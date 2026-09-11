# Linux USB access

Canoe Boot Manager needs permission to open the CANOE-BDS managed USB export.
On a desktop Linux system using udev and systemd-logind, install its scoped
[uaccess rule](https://github.com/1vivy/canoe-nusb-storage/blob/bbc4efa4dc9a47cf51319ac3a1b5d224b7e344f4/contrib/udev/70-canoe-managed-usb.rules) once:

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

This downloads the existing canoe-nusb-storage rule, installs it, reloads udev,
and applies it to an already connected `1209:ca0f` export. It grants access to
the active local user. It does not make all USB devices writable, mount a disk,
or change Fastboot permissions. Keep the `70-canoe-managed-usb.rules` filename:
it must run before `73-seat-late.rules` applies the access list.

Return to the application and select **Check connection**. The application can
reuse a previously granted device without reopening its chooser. You can also
unplug and reconnect the cable. The scoped udev trigger itself does not reset
USB, so it may produce no browser connection event. If the browser has not yet
been granted access, use **Choose USB device** or **Connect USB storage**.

Fastboot/Fastbootd may need your distribution's Android USB rules separately.
A working Fastboot connection does not establish access to CANOE-BDS managed
storage. Do not run the browser as root or use world-writable USB permissions.
This rule is for an active local login; SSH sessions and headless services need
their own narrowly scoped access policy.

See the [rule owner's installation notes](https://github.com/1vivy/canoe-nusb-storage/blob/bbc4efa4dc9a47cf51319ac3a1b5d224b7e344f4/contrib/udev/README.md)
and [Chrome's platform requirements](https://developer.chrome.com/docs/capabilities/build-for-webusb#linux).
