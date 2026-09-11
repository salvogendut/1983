#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rainbios=${1:-"$root/../rainbios"}
revision=b4d28b8e7e28054ad41530ff9bc26f5494da14f2
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
if test -n "$(git -C "$rainbios" status --porcelain --untracked-files=no)"; then
    echo "RainBIOS tracked files must be clean for a reproducible import" >&2
    exit 1
fi

make -C "$rainbios" \
    all msx2-sub-rom rainbios-disk-rom nms8250-disk-rom

check_rom() {
    expected=$1
    source=$2
    printf '%s  %s\n' "$expected" "$source" | sha256sum -c -
}

check_rom 12b3a887cfacb7e1e2657069f9ef780ef2cded06fb75188220ff3f193dcb4d5d \
    "$rainbios/build/rainbios_msx1.rom"
check_rom 3a2cee3c13f009022b42c19ddec567f3241da0fdc5d6137b019baca222c5d56e \
    "$rainbios/build/rainbios_msx2.rom"
check_rom 7b06e3e10990d2d815cf8b9a640e689167ab47b0f90df821cb48d0e7158049a0 \
    "$rainbios/build/rainbios_msx2_sub.rom"
check_rom e477477a99bf746d3ea3028e5a9a9313dc25e3c4251488e238fe8d94ca88b1a9 \
    "$rainbios/build/rainbios_disk.rom"
check_rom e477477a99bf746d3ea3028e5a9a9313dc25e3c4251488e238fe8d94ca88b1a9 \
    "$rainbios/build/rainbios_nms8250_disk.rom"

python3 "$root/tools/build-omega-unified-rom.py" \
    "$work/rainbios_omega.rom" \
    "$rainbios/build/rainbios_msx2.rom" \
    "$rainbios/build/rainbios_msx2_sub.rom" \
    "$rainbios/build/rainbios_disk.rom"
check_rom 415703ee35f31f05e68bcf6954e14a940b3332db52d420326f7f5fc2b3bef59f \
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
