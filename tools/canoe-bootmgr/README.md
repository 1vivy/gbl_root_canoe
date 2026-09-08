# canoe-bootmgr

A command and library for an already mounted Canoe boot root. Mounting, image
derivation, USB/partition access and guided deployment belong to other tools
or the manager application.

```sh
canoe-bootmgr --boot-root /mnt/canoe entry list
canoe-bootmgr --boot-root /mnt/canoe config show
canoe-bootmgr --boot-root /mnt/canoe entry mode --id android-a --mode 2
canoe-bootmgr --boot-root /mnt/canoe default set android-a
```

Use `--json` for machine-readable output. Errors are nonzero; command usage is
exit 2. Configuration edits validate inputs and report publication/flush errors.
They do not collect Android deployment evidence, infer formatting requirements,
run a recovery workflow, or discover dependencies. The application's separate
`canoe-manager` worker owns those workflows and uses this same configuration
library.

Prepared image operations use explicit sources and never discover devices:

```sh
canoe-bootmgr --boot-root /mnt/canoe loader install --slot a --from ./prepared
canoe-bootmgr --boot-root /mnt/canoe loader show --slot a
canoe-bootmgr --boot-root /mnt/canoe bls install --name linux.conf --entry linux.conf \
  --artifact linux/Image=./Image --artifact linux/initrd=./initrd
canoe-bootmgr --boot-root /mnt/canoe bls remove --name linux.conf
```

Use `--replace` explicitly to replace existing loader/BLS files. Loader commands
validate ARM64 PE, GM2P and TZ-map formats with the shared image parsers. They do
not create entries, change modes, infer a signing-key waiver or rotate backups.
BLS removal removes only its entry. Installation requires all referenced images,
rejects case collisions and reserved Canoe paths, and publishes the entry last.

Each file is staged and flushed before publication. Several files are not one
atomic filesystem operation: errors stop the command and earlier publications
remain. The manager owns review, snapshots, readback and operation recovery.
File operations retain a confined directory handle; do not keep a library root
alive while attempting to unmount or eject its filesystem.
