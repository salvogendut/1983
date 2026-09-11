#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rainbios=${1:-"$root/../rainbios"}
revision=54a2591b8455651722ae7dfad07d7c9460d30065
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if test ! -e "$rainbios/.git"; then
    echo "RainBIOS checkout not found: $rainbios" >&2
    echo "usage: $0 [PATH_TO_RAINBIOS]" >&2
    exit 1
fi

actual=$(git -C "$rainbios" rev-parse HEAD)
if test "$actual" != "$revision"; then
    echo "RainBIOS revision mismatch" >&2
    echo "expected: $revision" >&2
    echo "actual:   $actual" >&2
    exit 1
fi
if test -n "$(git -C "$rainbios" status --porcelain)"; then
    echo "RainBIOS checkout must be clean for a reproducible import" >&2
    exit 1
fi

make -C "$rainbios" \
    all msx2-sub-rom rainbios-disk-rom nms8250-disk-rom

check_rom() {
    expected=$1
    source=$2
    printf '%s  %s\n' "$expected" "$source" | sha256sum -c -
}

check_rom a1eaaf6230c51e669d5842d6038bc0abe3f6b890cc9f6c6d881ad582d03f8d66 \
    "$rainbios/build/rainbios_msx1.rom"
check_rom 68378ab2bb94452f2151b6b5a2d18f3315cd196830b09b7548b4a5828f64b9a0 \
    "$rainbios/build/rainbios_msx2.rom"
check_rom 7b06e3e10990d2d815cf8b9a640e689167ab47b0f90df821cb48d0e7158049a0 \
    "$rainbios/build/rainbios_msx2_sub.rom"
check_rom 7e934d5fcbf107be355e4cd171ef4430f40723b4039947d8de4b5ac82a0e8db9 \
    "$rainbios/build/rainbios_disk.rom"
check_rom 7e934d5fcbf107be355e4cd171ef4430f40723b4039947d8de4b5ac82a0e8db9 \
    "$rainbios/build/rainbios_nms8250_disk.rom"

python3 "$root/tools/build-omega-unified-rom.py" \
    "$work/rainbios_omega.rom" \
    "$rainbios/build/rainbios_msx2.rom" \
    "$rainbios/build/rainbios_msx2_sub.rom" \
    "$rainbios/build/rainbios_disk.rom"
check_rom 8dfbfea24f2ef836403a3186e9e50bc75f5b1320b48ac6fe36abb2cef10f37bd \
    "$work/rainbios_omega.rom"

install -m 0644 "$rainbios/build/rainbios_msx1.rom" \
    "$root/ROMS/rainbios_msx1.rom"
install -m 0644 "$rainbios/build/rainbios_msx2.rom" \
    "$root/ROMS/rainbios_msx2.rom"
install -m 0644 "$rainbios/build/rainbios_msx2_sub.rom" \
    "$root/ROMS/rainbios_msx2_sub.rom"
install -m 0644 "$rainbios/build/rainbios_disk.rom" \
    "$root/ROMS/rainbios_disk.rom"
install -m 0644 "$rainbios/build/rainbios_nms8250_disk.rom" \
    "$root/ROMS/rainbios_nms8250_disk.rom"
install -m 0644 "$work/rainbios_omega.rom" \
    "$root/ROMS/rainbios_omega.rom"

echo "Imported RainBIOS MSX1 and Omega MSX2 firmware from $revision"
