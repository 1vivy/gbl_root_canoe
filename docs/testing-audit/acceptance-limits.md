# Acceptance limits and exploration

Passing these suites establishes the stated decisions and byte/ABI contracts against their fixtures. It does not establish that a menu looks good, guidance is clear, a workflow is discoverable, or a particular phone can boot every possible firmware combination.

## Human or agent exploration

Review menu layout, spacing, colors, clipping, wording, scrolling and interaction pacing using the emitted console frames and an actual simulator or device session as appropriate. `test_menu_selection` can emit its frame/grid/attribute output for inspection; the tests no longer freeze a centered coordinate, color or English caption. Key selection, cancellation, row bounds and prevention of unintended automatic boot remain executable behavior.

Review the activated KSU WebUI, hosted Deploy scenarios, tool picker/list, entry maintenance and connection guidance in CBM's real simulator. The module shell fixture establishes manager-only behavior and preference preservation. It cannot establish WebUI presentation, Android picker behavior or kernel-specific activation.

Record the build/source, environment, scenario, observed result and any missing acceptance separately. Do not turn subjective feedback into a string/pixel snapshot merely to make it appear covered. Exact bytes remain appropriate for wire formats, serialized configuration semantics and qualified image transformations.

## This audit's evidence

This work ran host-native test fixtures and workflow lint. It made no physical-phone writes, reboots, new BDS boot tests, Windows USB claims or new KSU guest qualification. Test-only changes do not require relinking BDS; the existing published firmware images, pins, branches and tags remain unchanged.

The local aggregate excludes the ignored external ABL corpus fixture, ignored privileged container-mount fixture and `legacy-ext4-tests` feature. Historical `canoe-ext4` libext2fs/Wine fixtures do not qualify the current browser Rust ext4 stack. Historical extractor smoke scripts and optional WASM qualification exports are inventory items, not automatic pass claims. Run them only for their actual producer/toolchain change and record that execution.

The imported UEFI Ext4Pkg checksum matrix uses independent e2fsprogs-generated filesystems, including Android-style recovery flags. Keeping that driver compatibility test does not authorize CBM to add another filesystem integrity gate. Filesystems remain responsible for their internals; callers validate the file operations they requested.

CI classification was checked locally and linted; the new duty-based workflow has not yet run on GitHub in this audit. Report its future measured time separately from the savings implied by skipped jobs.
