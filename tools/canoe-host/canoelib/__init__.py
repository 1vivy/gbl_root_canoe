"""The canoe host-side toolkit: derive, prepare and install the boot chain.

One implementation for both toolkits. The Linux archive runs it on the host's
own `python3`; the Windows archive ships an embeddable CPython beside it. That
embeddable runtime has no pip and no site-packages, so everything under this
package is **stdlib only** - a third-party import here would break the shipped
Windows toolkit, not just the build.

The device-side transaction is deliberately NOT here: it stays in
`tools/canoe-device/canoe_device_install.sh`, because it runs on the device.
"""

from __future__ import annotations

import sys

if sys.version_info < (3, 11):  # pragma: no cover - guards the launcher, not a path
    raise SystemExit(
        f"canoe: error: python 3.11 or newer is required, found {sys.version.split()[0]}"
    )
