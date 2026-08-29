#!/bin/sh
set -eu

: "${SYMBOS_IMAGE:?set SYMBOS_IMAGE to a partitioned Nextor/SymbOS image}"

source_root=${srcdir:-.}
offset=${SYMBOS_IMAGE_OFFSET:-16384}
frames=${SYMBOS_FRAMES:-8000}
work=$(mktemp -d)
image=$work/symg9k.img
autoexec=$work/AUTOEXEC.BAT
screenshot=${SYMBOS_SCREENSHOT:-$work/symg9k.ppm}
cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

command -v mcopy >/dev/null
command -v node >/dev/null
cp "$SYMBOS_IMAGE" "$image"
awk '{ sub(/\r$/, ""); printf "%s\r\n", $0 }' \
    "$source_root/diagnostics/SYMG9K-AUTOEXEC.BAT" >"$autoexec"
mcopy -o -i "$image@@$offset" "$autoexec" ::AUTOEXEC.BAT

./1983 --config /dev/null \
    --models "$source_root/1983-models.conf" \
    --model omega-msx2 --memory 512 \
    --sunrise-rom "$source_root/ROMS/Nextor-2.1.1.SunriseIDE.ROM" \
    --ide "$image" --ide-mode read-only \
    --powergraph-v9990 --no-unapi \
    --headless --unthrottled --exit-after "$frames" \
    --screenshot "$screenshot"

node "$source_root/diagnostics/check-v9990-symbos.js" "$screenshot"
