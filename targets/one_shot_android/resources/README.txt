CANOE one-shot Android install package

Required before running anything: a root shell on the phone (`id -u` is 0) and
an already-mounted ext4 persist directory (commonly /mnt/vendor/persist). The
installer uses the active slot's stock ABL and vbmeta as read-only derivation
sources; the ABL does not need to contain the vulnerable loader.

  cd /data/local/tmp && unzip -o canoe-one-shot-<version>-android-arm64.zip -d canoe && cd canoe
  chmod 755 install-canoe.sh bin/*

The archive stores those executable, but Android's unzip does not always
restore Unix permissions, so chmod them rather than reading the resulting
failure as a permission problem.

  ./install-canoe.sh --mode 1 --persist-mount /mnt/vendor/persist \
    --work-dir /data/local/tmp/canoe-work

Without --apply, that prints the plan and changes nothing. Repeat the same
command with --apply only after review; the confirmed run creates the FAT
container in persist and writes BDS to raw efisp. Device mode is always
explicit; there is no inherited or default mode.

Before --apply, check getenforce. If it says Enforcing, run setenforce 0 as
root and verify getenforce now says Permissive (or proceed if already Disabled).
Root alone does not authorize kernel loop I/O on persist/efisp.fat under
Enforcing. The installer neither checks nor changes SELinux, and this archive
does not install the KernelSU policy. Permissive reduces
SELinux protection for the entire device, not only efisp.fat. Restore
Enforcing afterward with setenforce 1 (or reboot). Android-side loop access
under Enforcing will then require a separately active policy grant; firmware
boot does not depend on Android SELinux.

--work-dir must name an unused scratch directory. It holds temporary staged
loader files and diagnostic readback output, not a rollback backup. An
independent off-device persist backup and separate recovery path are recommended
before --apply: the installer does not save the old raw efisp and has no built-in
efisp rollback copy.

install-canoe.sh reads the active slot's ABL and vbmeta without writing them,
derives the prepared boot_a.efi/boot_b.efi loader and its sidecars, creates the
FAT container in persist, and writes BDS only to raw efisp. It never flashes an
ABL. The canoe-provision creation primitive refuses an existing
persist/efisp.fat rather than overwriting it.

Preparation is not bootability. A stock ABL has no efisp redirect, so this
setup alone cannot launch Canoe. Before expecting it to boot, obtain a
compatible signed vulnerable ABL independently and install it yourself or with
another separately authorized tool. Never write the prepared boot_<slot>.efi
to an ABL partition: it is modified and unsigned, so XBL rejects it. It belongs
only in the managed boot root, where BDS can load it with security bypassed.

BDS.efi and efisp/tools/ are the reviewed ARM64 UEFI images from this firmware
release. bin/ holds the Android ARM64 commands the installer drives.
Nothing in this archive runs, flashes, installs, reboots or formats on its own.
The firmware release manifest records the ZIP's exact length and SHA-256; its
EFI catalogue and release checksums verify the firmware members against the
standalone assets.
