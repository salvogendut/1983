#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rainbios=${1:-"$root/../rainbios"}
revision=381c36dc39aeeaa53f876a92ab6517b1f3d2eb74
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if test ! -d "$rainbios/.git"; then
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
    msx2-main-rom msx2-sub-rom rainbios-disk-rom nms8250-disk-rom

check_rom() {
    expected=$1
    source=$2
    printf '%s  %s\n' "$expected" "$source" | sha256sum -c -
}

check_rom 04682c2fd04858d51e7a3dc4b75a6c2c62affe2e2fe5fbb62a8e1491f4d91709 \
    "$rainbios/build/rainbios_msx2.rom"
check_rom f8230c25f45db88d4c032e59dd714c4bf65c1b3300000fc20a046fa32ef984ed \
    "$rainbios/build/rainbios_msx2_sub.rom"
check_rom ef6c94e7a8896ddc3109cc20b260e2c11d56a37e6baeeed23bb72dca69421cbc \
    "$rainbios/build/rainbios_disk.rom"
check_rom ef6c94e7a8896ddc3109cc20b260e2c11d56a37e6baeeed23bb72dca69421cbc \
    "$rainbios/build/rainbios_nms8250_disk.rom"

python3 "$root/tools/build-omega-unified-rom.py" \
    "$work/rainbios_omega.rom" \
    "$rainbios/build/rainbios_msx2.rom" \
    "$rainbios/build/rainbios_msx2_sub.rom" \
    "$rainbios/build/rainbios_disk.rom"
check_rom 5d0568532d8c2dedf8ba9ce29781423567ec90a9b28306c1ec45443e54333b7b \
    "$work/rainbios_omega.rom"

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

echo "Imported RainBIOS Omega MSX2 firmware from $revision"
