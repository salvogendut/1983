#!/bin/sh

set -eu

usage() {
    echo "usage: $0 OUTPUT.IMG [MSXDOS2.SYS COMMAND2.COM]" >&2
    exit 2
}

[ "$#" -eq 1 ] || [ "$#" -eq 3 ] || usage

image=$1
partition_start=32
partition_sectors=65248
image_sectors=65280
partition_offset=16384

if [ -e "$image" ]; then
    echo "$0: refusing to overwrite existing file: $image" >&2
    exit 1
fi

for command in truncate sfdisk mkfs.fat; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "$0: missing required command: $command" >&2
        exit 1
    fi
done
if [ "$#" -eq 3 ] && ! command -v mcopy >/dev/null 2>&1; then
    echo "$0: copying DOS files requires mcopy from mtools" >&2
    exit 1
fi

cleanup=true
trap 'if [ "$cleanup" = true ]; then rm -f -- "$image"; fi' EXIT HUP INT TERM

truncate -s "$((image_sectors * 512))" "$image"
sfdisk "$image" >/dev/null <<EOF
label: dos
unit: sectors
first-lba: $partition_start

start=$partition_start, size=$partition_sectors, type=1
EOF

# BERT SCSI V2.7 expects its type-1 partition to contain FAT12. A 16-sector
# cluster keeps this roughly 32 MiB partition below FAT12's cluster limit.
mkfs.fat -a -R 1 -r 256 -M 0xf0 -g 32/9 --offset "$partition_start" \
    -F 12 -s 16 -n SCSIDOS "$image" "$((partition_sectors / 2))" \
    >/dev/null

if [ "$#" -eq 3 ]; then
    [ -f "$2" ] || { echo "$0: file not found: $2" >&2; exit 1; }
    [ -f "$3" ] || { echo "$0: file not found: $3" >&2; exit 1; }
    mcopy -o -i "$image@@$partition_offset" "$2" ::MSXDOS2.SYS
    mcopy -o -i "$image@@$partition_offset" "$3" ::COMMAND2.COM
fi

cleanup=false
trap - EXIT HUP INT TERM
echo "Created BERT-compatible SCSI image: $image"
if [ "$#" -eq 1 ]; then
    echo "Copy legally obtained MSXDOS2.SYS and COMMAND2.COM into its FAT12 partition."
fi
