#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CORPUS="$ROOT/corpus"
SRC="$CORPUS/seed-files"
MANIFEST="$CORPUS/manifest.sha256"
rm -rf "$CORPUS"
mkdir -p "$SRC" "$CORPUS/images" "$CORPUS/manifests"
printf '%s\n' 'canoe ext4 preflight seed file.' > "$SRC/seed.txt"
printf '%s\n' 'persist config before libext2fs mutation.' > "$SRC/settings.conf"
printf '%s\n' 'nested known content for delete check.' > "$SRC/delete-me"
printf '%s\n' 'second nested file retained across mutation.' > "$SRC/nested.txt"
: > "$MANIFEST"

# Keep the explicit Android-persist baseline while testing one modern feature
# at a time. This is the same 20-image matrix used by the libext2fs preflight.
BASE='^resize_inode,^large_file,^huge_file,^dir_nlink,^extra_isize,^metadata_csum_seed,^orphan_file,^inline_data,^casefold,metadata_csum,64bit,flex_bg,ext_attr,dir_index,filetype,sparse_super2,has_journal,extent'
for size in 16M 128M; do
  for block in 1024 4096; do
    for variant in baseline casefold orphan_file metadata_csum_seed inline_data; do
      extra=''
      [ "$variant" = baseline ] || extra=,"$variant"
      name="${size%M}m-${block}b-${variant}"
      image="$CORPUS/images/$name.img"
      truncate -s "$size" "$image"
      mke2fs -q -F -t ext4 -b "$block" -I 256 \
        -O "$BASE$extra" -L "canoe-$name" "$image"
      cat > "$CORPUS/$name.debugfs" <<EOF
mkdir /persist
mkdir /persist/config
write $SRC/seed.txt /persist/seed.txt
write $SRC/settings.conf /persist/config/settings.conf
write $SRC/delete-me /persist/config/delete-me
write $SRC/nested.txt /persist/config/nested.txt
EOF
      debugfs -w -f "$CORPUS/$name.debugfs" "$image" >/dev/null 2>&1
      e2fsck -fy "$image" >/dev/null 2>&1
      {
        printf '%s\n' "# image=$name"
        tune2fs -l "$image" | sed -n '/Filesystem features:/p'
        for pair in \
          'persist/seed.txt:seed.txt' \
          'persist/config/settings.conf:settings.conf' \
          'persist/config/delete-me:delete-me' \
          'persist/config/nested.txt:nested.txt'; do
          path=${pair%%:*}; key=${pair#*:}
          sha256sum "$SRC/$key" | awk -v image="$name" -v path="/$path" \
            '{print image "\t" path "\t" $1}' >> "$MANIFEST"
        done
      } > "$CORPUS/manifests/$name.features"
    done
  done
done
count=$(find "$CORPUS/images" -type f -name '*.img' | wc -l)
test "$count" -eq 20
printf 'Generated %s images; manifest: %s\n' "$count" "$MANIFEST"
