# Reinstall from gbl-chainload, gbl_root_canoe 6.3.5, or another EFISP mod

The same steps apply to 1vivy's gbl-chainload, gbl_root_canoe 6.3.5 and earlier,
and any other mod that wrote the raw `efisp` partition.

Canoe 7 keeps its boot root in `persist/efisp.fat`. Nothing from the old ext4
`efisp/` directory is migrated, imported or copied. You get a new boot root.

Open the hosted app and choose **Fresh install / redeploy**. That is the
reinstallation path; there is no separate gbl-chainload, 6.x, custom-ROM or
flasher-package procedure to follow. Tell the app that the phone currently has
another EFISP modification, then follow its preparation, cleanup, backup and
format-data assessment. It waits for review before every write.

Two things are worth knowing before you start:

- **Your Android entries are rebuilt for you.** Deployment publishes the
  `android-a`, `android-b` and `android-backup` rows together with their loader
  triplets. Only extra EFI or BLS rows you added yourself need recreating, and
  that is advanced-user work.
- **The boot root is the root.** There is no `efisp/` prefix any more, so
  `\efisp\vmlinuz` becomes `\vmlinuz`.

## What to bring forward

- Note your current mode and keep the firmware ABL and vbmeta you used.
- Preserve any image you would want for recovery.
- Copy the titles, options and payloads of extra EFI/BLS rows you created.

The app rebuilds the normal Android rows and loader triplets. If it offers to
remove the old `persist/efisp`, review and accept that cleanup before the new
container is provisioned: leftover files reduce the free space available to the
container. Recreate only your extra EFI/BLS rows afterward, using Entries or the
[mounted-root CLI](./commands.md).

A missing `efisp.fat` does not make an older installation a first-time mod, and
having used a previous mod does not by itself determine whether phone data must
be formatted. Give the app the requested history and use the assessment it
shows.

Keep a recovery route while you are changing bootloaders. Revert recovers an
incomplete recorded operation; it is not an importer, and it cannot pull back an
arbitrary old firmware snapshot.
