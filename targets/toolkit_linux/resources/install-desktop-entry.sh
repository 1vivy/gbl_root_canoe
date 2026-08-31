#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
applications_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
output="$applications_dir/canoe-boot-manager.desktop"
mkdir -p "$applications_dir"
escaped_dir=$(printf '%s' "$script_dir" | sed 's/[\\&|]/\\&/g')
sed "s|@CANOE_ROOT@|$escaped_dir|g" "$script_dir/canoe-boot-manager.desktop" > "$output"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$applications_dir"
fi
printf '%s\n' "$output"
