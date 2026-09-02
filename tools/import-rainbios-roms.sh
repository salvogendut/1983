#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
rainbios=${1:-"$root/../rainbios"}
revision=abb1a34df3830fa0771b4a4bd35889207c812b6d
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

check_rom 09bb82e378f03cf6ebc18d7fc8711cba2ad14f9f313aca24db40f909ae2f90c9 \
    "$rainbios/build/rainbios_msx1.rom"
check_rom ec1a75ad620e91610f8b8f4f825e0097685dde5325620947f8b3c63d528f9118 \
    "$rainbios/build/rainbios_msx2.rom"
check_rom f79e9664d94f78640331232c74fd67032fff8dcb7e667ee18f16e79dd8e9d486 \
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
check_rom 77f9c812b534e03ebd8c7bd0aee14b78e6ca7a4b030a947780291dd910470773 \
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
