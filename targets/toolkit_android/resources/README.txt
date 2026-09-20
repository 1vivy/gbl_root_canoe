Canoe Android command tools

Getting it onto the phone, from a root shell:

  adb push toolkit_android.zip /data/local/tmp/
  adb shell
  su
  cd /data/local/tmp && unzip -o toolkit_android.zip -d canoe && cd canoe
  chmod 755 install-canoe.sh bin/*

The archive stores these executable, but Android's unzip does not always
restore Unix permissions, so chmod them rather than reading a resulting
"Permission denied" as a root problem.

--persist-mount is the already-mounted ext4 persist directory, commonly
/mnt/vendor/persist. Confirm it with `mount | grep persist` before running;
the script will not mount, unmount or guess it.

  ./install-canoe.sh --mode 1 --persist-mount /mnt/vendor/persist \
    --backup-dir /data/local/tmp/canoe-rollback

That prints the plan and changes nothing. Repeat it with --apply to execute
exactly that plan.

For a rooted Android shell, install-canoe.sh is the one-pass installer. It
requires the active slot's firmware as its derivation source, an explicit
--mode 0|1|2, a mounted persist directory and a new rollback directory. Its
default is plan-only; --apply confirms the displayed raw efisp write. Raw efisp
is the only partition written: the active slot keeps the vulnerable ABL that
abl-check proved it already carries, since that is what dispatches to efisp.
Before writing it saves and verifies rollback images of the active ABL and
efisp, and it publishes the loader, sidecars, EFI tools and explicit default
entry while efisp.fat is mounted. It unmounts the container before the raw
write.

The individual bin/canoe-image, bin/canoe-provision and bin/canoe-bootmgr
commands remain available for manual preparation. The former build.sh wrapper
is retired; install-canoe.sh is a non-interactive, root-only orchestration
wrapper, not a deployment wizard. It does not assess userdata or firmware
suitability beyond checking for Canoe's supported vulnerable loader. For guided
review, readback and recovery, use Canoe Boot Manager (KernelSU module).

See wiki/docs/commands.md in the source repository. Run each command with
--help for its exact interface. Never mix raw writes with a mounted filesystem.
