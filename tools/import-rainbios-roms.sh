#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rainbios=${1:-"$root/../rainbios"}
revision=00e6fe99aec1f419ebcfab473c8dc2ebfeccc7b5
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
    all msx2-sub-rom rainbios-disk-rom nms8250-disk-rom

check_rom() {
    expected=$1
    source=$2
    printf '%s  %s\n' "$expected" "$source" | sha256sum -c -
}

check_rom 1b7e7ebfdf691fad72e2409f5615b5a7a05df61bfe80e4b98410a34b5feb68ef \
    "$rainbios/build/rainbios_msx1.rom"
check_rom 2aa867fa0a66543dcebf0ebe8f6b0b3522384405e6a45ba6a48c65d6ae9dc0d6 \
    "$rainbios/build/rainbios_msx2.rom"
check_rom 7b06e3e10990d2d815cf8b9a640e689167ab47b0f90df821cb48d0e7158049a0 \
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
check_rom 72931f79ce085f38ea114705f6a8959e3b39c36899e59e41dafa8cf718f41693 \
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
