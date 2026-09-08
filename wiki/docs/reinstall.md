# Reinstall from gbl-chainload or Canoe 6.3.5 and earlier

Canoe 7 uses `persist/efisp.fat`. It does **not** migrate the old ext4 `efisp/`
directory, import its configuration, copy its loaders, or delete its contents.
Reinstalling creates a new boot root; recreate boot entries deliberately.

1. Record the old configuration, boot entry titles/options and payload sources
   before replacing raw efisp. Keep any images needed for recovery separately.
2. In desktop Deploy, select **Used an EFISP mod before** (or Unsure if the
   history is unclear). Do not identify this as the first EFISP modification
   merely because the new FAT container is absent.
3. Follow [installation](./install.md), selecting compatible firmware images
   and the intended mode. Review the data assessment; previous mod presence
   alone cannot establish the effective signing identity or running mode.
4. Recreate ordinary EFI and BLS entries in Boot entries or with the
   [mounted-root CLI](./commands.md). Copy the payloads you intentionally want
   into the new FAT root. Paths are relative to this root, with no `efisp/`
   prefix. Rebuild managed Android loaders from the selected firmware images.
5. Inspect the recreated entries and choose a default. The old directory is
   left untouched and unused; it can be removed manually later if desired.

Keep an explicit recovery route while changing bootloaders. Application Revert
is recovery for an incomplete recorded operation, not an importer or a way to
select an arbitrary historical firmware snapshot.
