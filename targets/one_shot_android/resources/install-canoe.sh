#!/bin/sh
# One-shot Canoe installer for a rooted Android shell.
# Baked assumptions: this runs as uid 0, and the ACTIVE slot's ABL/vbmeta are
# the firmware derivation source, even when the ABL has no efisp redirect.
#
# Raw efisp is the ONLY partition this writes. The active slot's ABL is read
# and never written. Do not "improve" this by flashing a patched ABL: patching
# edits the image, so it is no longer signed, and XBL authenticates abl on the
# boot chain. A patched ABL on the booting slot is rejected before it runs and
# costs an EDL recovery. The patched image belongs in the boot root as
# boot_<slot>.efi, which BDS loads with security bypassed because it is
# unsigned, and whose efisp lookup is patched out by design.
set -eu

usage() {
    cat <<'EOF'
Usage: install-canoe.sh --mode 0|1|2 --persist-mount DIR --work-dir DIR [--apply]

Without --apply, validates the active-slot source and prints the exact write
plan without changing storage. --apply confirms that destructive plan.
EOF
}

die() { echo "install-canoe: $*" >&2; exit 1; }

MODE=
PERSIST_MOUNT=
WORK_DIR=
APPLY=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --mode)
            [ "$#" -ge 2 ] || die '--mode requires 0, 1 or 2'
            MODE=$2
            shift 2
            ;;
        --persist-mount)
            [ "$#" -ge 2 ] || die '--persist-mount requires a directory'
            PERSIST_MOUNT=$2
            shift 2
            ;;
        --work-dir)
            [ "$#" -ge 2 ] || die '--work-dir requires a directory'
            WORK_DIR=$2
            shift 2
            ;;
        --apply)
            APPLY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

case "$MODE" in 0|1|2) ;; *) die 'an explicit --mode 0, 1 or 2 is required' ;; esac
[ -n "$PERSIST_MOUNT" ] || die 'an explicit --persist-mount is required'
[ -n "$WORK_DIR" ] || die 'an explicit --work-dir is required'
[ "$(id -u)" = 0 ] || die 'root is required (id -u must be 0)'

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BIN_DIR=$SCRIPT_DIR/bin
BY_NAME_DIR=${CANOE_BY_NAME_DIR:-/dev/block/by-name}
SLOT_SUFFIX=$(getprop ro.boot.slot_suffix 2>/dev/null || true)
case "$SLOT_SUFFIX" in
    _a) SLOT=a ;;
    _b) SLOT=b ;;
    *) die "ro.boot.slot_suffix must identify the active slot (_a or _b); got '$SLOT_SUFFIX'" ;;
esac

ABL_PART=$BY_NAME_DIR/abl$SLOT_SUFFIX
VBMETA_PART=$BY_NAME_DIR/vbmeta$SLOT_SUFFIX
EFISP_PART=$BY_NAME_DIR/efisp
BDS_IMAGE=$SCRIPT_DIR/BDS.efi
STAGED=$WORK_DIR/staged
BOOT_MOUNT=$WORK_DIR/boot-root

for command in dd sha256sum sync losetup mount umount cp mkdir; do
    command -v "$command" >/dev/null 2>&1 || die "required command is unavailable: $command"
done
for tool in canoe-image canoe-provision canoe-bootmgr; do
    [ -x "$BIN_DIR/$tool" ] || die "missing executable: $BIN_DIR/$tool"
done
[ -r "$ABL_PART" ] || die "active-slot ABL is not readable: $ABL_PART"
[ -r "$VBMETA_PART" ] || die "active-slot vbmeta is not readable: $VBMETA_PART"
[ -r "$EFISP_PART" ] || die "raw efisp is not readable: $EFISP_PART"
[ -r "$BDS_IMAGE" ] || die "BDS image is not readable: $BDS_IMAGE"
[ -d "$PERSIST_MOUNT" ] || die "persist mount is not a directory: $PERSIST_MOUNT"
[ -d "$SCRIPT_DIR/efisp/tools" ] || die "EFI tools are missing: $SCRIPT_DIR/efisp/tools"

print_plan() {
    echo "Active derivation slot: $SLOT"
    echo "Derivation source, read only and never written: $ABL_PART"
    echo "Single raw write: partition=$EFISP_PART image=$BDS_IMAGE"
    echo "Boot root: $PERSIST_MOUNT/efisp.fat entry boot_$SLOT.efi mode $MODE"
    echo 'Boot prerequisite: an active ABL without the efisp redirect will not launch Canoe; install a compatible signed vulnerable ABL separately before expecting it to boot.'
    echo "Work directory: $WORK_DIR (not a rollback backup)"
}

print_plan
if [ "$APPLY" -ne 1 ]; then
    echo 'Plan only: no storage was changed. Re-run with --apply to confirm this plan.'
    exit 0
fi

[ -w "$EFISP_PART" ] || die "raw efisp is not writable: $EFISP_PART"
[ ! -e "$WORK_DIR" ] || die "work directory already exists: $WORK_DIR"

MOUNTED=0
LOOP_ATTACHED=0
LOOP_DEVICE=
CONTAINER_CREATED=0
WRITE_PHASE=none
detach_loop() {
    # Some kernels auto-release the loop at umount, so -d then reports ENXIO.
    if losetup -d "$LOOP_DEVICE" >/dev/null 2>&1 ||
       ! losetup "$LOOP_DEVICE" >/dev/null 2>&1; then
        LOOP_ATTACHED=0
        LOOP_DEVICE=
        return 0
    fi
    return 1
}

cleanup() {
    status=$?
    cleanup_storage_ok=1
    trap - 0 INT TERM HUP
    if [ "$MOUNTED" -eq 1 ]; then
        if umount "$BOOT_MOUNT" >/dev/null 2>&1; then
            MOUNTED=0
        else
            cleanup_storage_ok=0
            echo "Failed to unmount $BOOT_MOUNT; backing storage was left intact." >&2
        fi
    fi
    if [ "$LOOP_ATTACHED" -eq 1 ] && [ "$MOUNTED" -eq 0 ]; then
        if ! detach_loop; then
            cleanup_storage_ok=0
            echo "Failed to detach $LOOP_DEVICE; backing storage was left intact." >&2
        fi
    fi
    if [ "$status" -ne 0 ] && [ "$CONTAINER_CREATED" -eq 1 ] &&
       [ "$WRITE_PHASE" = none ] && [ "$cleanup_storage_ok" -eq 1 ]; then
        "$BIN_DIR/canoe-provision" remove --persist-directory "$PERSIST_MOUNT" >/dev/null 2>&1 || true
    fi
    if [ "$status" -ne 0 ] && [ "$WRITE_PHASE" != none ]; then
        echo 'A raw write started but the installation did not complete; no prior efisp image was saved by this installer.' >&2
    fi
    exit "$status"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP

hash_file() { sha256sum "$1" | cut -d ' ' -f 1; }
write_image() {
    image=$1
    partition=$2
    readback=$3
    bytes=$(wc -c < "$image" | tr -d '[:space:]')
    [ "$bytes" -gt 0 ] || die "refusing to write an empty image: $image"
    dd if="$image" of="$partition" bs=4194304
    sync
    dd if="$partition" of="$readback" bs="$bytes" count=1
    [ "$(hash_file "$image")" = "$(hash_file "$readback")" ] ||
        die "partition readback does not match $image: $partition"
}

mkdir -p "$WORK_DIR"
"$BIN_DIR/canoe-image" build \
    --abl "$ABL_PART" \
    --vbmeta "$VBMETA_PART" \
    --staged "$STAGED" \
    --tools "$BIN_DIR" \
    --efisp-tools "$SCRIPT_DIR/efisp/tools"

"$BIN_DIR/canoe-provision" create --persist-directory "$PERSIST_MOUNT"
CONTAINER_CREATED=1
LOOP_DEVICE=$(losetup -f) || die 'no free loop device is available'
losetup "$LOOP_DEVICE" "$PERSIST_MOUNT/efisp.fat"
LOOP_ATTACHED=1
mkdir -p "$BOOT_MOUNT"
mount -t vfat -o rw "$LOOP_DEVICE" "$BOOT_MOUNT"
MOUNTED=1
"$BIN_DIR/canoe-bootmgr" --boot-root "$BOOT_MOUNT" loader install --slot "$SLOT" --from "$STAGED"
mkdir -p "$BOOT_MOUNT/tools"
for tool in "$STAGED"/tools/*.efi; do
    [ -f "$tool" ] || die 'no staged EFI tools were produced'
    cp "$tool" "$BOOT_MOUNT/tools/"
done
"$BIN_DIR/canoe-bootmgr" --boot-root "$BOOT_MOUNT" entry set \
    --id "android-$SLOT" --title "Android $SLOT" --image "boot_$SLOT.efi" \
    --mode "$MODE" --role active --default
sync
umount "$BOOT_MOUNT"
MOUNTED=0
detach_loop || die "Failed to detach $LOOP_DEVICE; backing storage was left intact."

WRITE_PHASE=efisp-started
write_image "$BDS_IMAGE" "$EFISP_PART" "$WORK_DIR/efisp.readback"
WRITE_PHASE=complete

echo 'Canoe installation completed and the efisp write passed readback verification.'
echo "Preparation outputs remain in: $WORK_DIR (not a rollback backup)"
print_plan
