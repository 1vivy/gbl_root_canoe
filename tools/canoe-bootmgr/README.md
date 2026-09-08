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

This is the first extraction checkpoint. Prepared-loader and BLS artifact
installation are being moved to this interface; do not use the old all-in-one
backend command reference for the new CLI.
