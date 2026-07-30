#!/bin/sh
set -eu

log=tests/test-cli-output.tmp
config=tests/test-cli-config.tmp
sunrise=tests/test-cli-sunrise.tmp
sdrom=tests/test-cli-sdmapper.tmp
sdimage=tests/test-cli-sdcard.tmp
megaflash=tests/test-cli-megaflash.tmp
cassette=tests/test-cli-cassette.tmp
first_run=tests/test-cli-first-run.tmp
source_root=${srcdir:-.}
trap 'rm -f "$log" "$config" "$sunrise" "$sdrom" "$sdimage" "$megaflash" "$cassette" "$first_run"' EXIT HUP INT TERM

rm -f "$first_run"
./1983 --config "$first_run" \
    --models "$source_root/1983-models.conf" \
    --headless --unthrottled --exit-after 0 \
    >"$log" 2>&1
grep -q '^model = cbios$' "$first_run"
grep -q 'cbios_main_msx1\.rom$' "$first_run"
grep -q 'cbios_logo_msx1\.rom$' "$first_run"
grep -q 'BIOS loaded, logo ROM loaded' "$log"

if ./1983 --mapper definitely-not-a-mapper >"$log" 2>&1; then
    echo "invalid mapper was accepted" >&2
    exit 1
fi
grep -q "expected auto, linear, ascii8, ascii16, konami" "$log"

printf '%s\n' \
    '[advanced]' \
    'notifications = off' >"$config"
./1983 --config "$config" --headless --unthrottled --exit-after 0 \
    >"$log" 2>&1
if grep -q "1983 - MSX / MSX2 emulator" "$log" ||
        grep -q "Machine catalogue:" "$log" ||
        grep -q "No BIOS loaded" "$log" ||
        grep -q "RTC CMOS:" "$log"; then
    echo "notifications off did not suppress startup information" >&2
    exit 1
fi
if ./1983 --config "$config" --bios missing-bios.rom \
        >"$log" 2>&1; then
    echo "missing BIOS was accepted while notifications were off" >&2
    exit 1
fi
grep -q "cannot load firmware set" "$log"

if ./1983 --config /dev/null --model definitely-not-a-model \
        >"$log" 2>&1; then
    echo "unknown catalogue model was accepted" >&2
    exit 1
fi
grep -q "unknown catalogue model" "$log"

printf '%s\n' \
    '[extensions]' \
    'extra_hardware = true' \
    'sunrise_ide = true' \
    "sunrise_rom = $sunrise" >"$config"
dd if=/dev/zero of="$sunrise" bs=131072 count=1 2>/dev/null
if ./1983 --config "$config" --cart2 missing.rom >"$log" 2>&1; then
    echo "cartridge was accepted in an extension-reserved slot" >&2
    exit 1
fi
grep -q "cartridge slot 2 unavailable: reserved by Sunrise IDE" "$log"

if ./1983 --config /dev/null --sunrise-rom missing-sunrise.rom \
        >"$log" 2>&1; then
    echo "missing Sunrise ROM was accepted" >&2
    exit 1
fi
grep -q "cannot load 128 KB Sunrise IDE ROM" "$log"

if ./1983 --config /dev/null --sunrise-rom "$sunrise" \
        --ide missing-ide.img >"$log" 2>&1; then
    echo "missing IDE image was accepted" >&2
    exit 1
fi
grep -q "cannot mount raw IDE image read-only" "$log"

if ./1983 --ide-mode unsafe >"$log" 2>&1; then
    echo "invalid IDE image mode was accepted" >&2
    exit 1
fi
grep -q "expected read-only or read-write" "$log"

if ./1983 --config /dev/null \
        --megaflash-rom missing-megaflash.rom \
        >"$log" 2>&1; then
    echo "missing MegaFlashROM image was accepted" >&2
    exit 1
fi
grep -q "cannot load MegaFlashROM" "$log"

dd if=/dev/zero of="$megaflash" bs=1048576 count=8 2>/dev/null
dd if=/dev/zero of="$sdimage" bs=512 count=2 2>/dev/null
./1983 --config /dev/null --megaflash-rom "$megaflash" \
    --megaflash-sd-a "$sdimage" --headless --unthrottled \
    --exit-after 0 >"$log" 2>&1
grep -q "MegaFlashROM SCC+ SD loaded in cartridge slot 2" "$log"
grep -q "MegaFlash SD A: $sdimage (read-only)" "$log"

if ./1983 --config /dev/null --sd-mapper-rom missing-sdmapper.rom \
        >"$log" 2>&1; then
    echo "missing SD Mapper ROM was accepted" >&2
    exit 1
fi
grep -q "cannot load 128/256 KB SD Mapper V2 ROM" "$log"

dd if=/dev/zero of="$sdrom" bs=131072 count=1 2>/dev/null
dd if=/dev/zero of="$sdimage" bs=512 count=2 2>/dev/null
if ./1983 --config /dev/null --sd-mapper-rom "$sdrom" \
        --sd-a missing-sdcard.img >"$log" 2>&1; then
    echo "missing SD card image was accepted" >&2
    exit 1
fi
grep -q "cannot mount SD Mapper card A image" "$log"

./1983 --config /dev/null --sd-mapper-rom "$sdrom" \
    --sd-a "$sdimage" --headless --unthrottled --exit-after 0 \
    >"$log" 2>&1
grep -q "SD Mapper V2 loaded in cartridge slot 2" "$log"
grep -q "SD A: $sdimage (read-only)" "$log"

if ./1983 --sd-mode unsafe >"$log" 2>&1; then
    echo "invalid SD image mode was accepted" >&2
    exit 1
fi
grep -q "expected read-only or read-write" "$log"

if ./1983 --floppy-mode unsafe >"$log" 2>&1; then
    echo "invalid floppy image mode was accepted" >&2
    exit 1
fi
grep -q "expected read-only or read-write" "$log"

if ./1983 --config /dev/null --cassette missing.cas >"$log" 2>&1; then
    echo "missing cassette was accepted" >&2
    exit 1
fi
grep -q "cannot load MSX CAS cassette image" "$log"

printf '\037\246\336\272\314\023\175\164\352\352\352\352\352\352\352\352\352\352\032' >"$cassette"
./1983 --config /dev/null --cassette "$cassette" --headless \
    --unthrottled --exit-after 0 >"$log" 2>&1
grep -q "Cassette inserted:" "$log"
grep -q 'ASCII; RUN"CAS:"' "$log"

./1983 --help >"$log"
grep -q -- "--model NAME" "$log"
grep -q -- "--models PATH" "$log"
grep -q -- "--cart1 PATH" "$log"
grep -q -- "--cart2 PATH" "$log"
grep -q -- "--mapper1 NAME" "$log"
grep -q -- "--mapper2 NAME" "$log"
grep -q -- "--sunrise-rom PATH" "$log"
grep -q -- "--sd-mapper-rom PATH" "$log"
grep -q -- "--sd-a PATH" "$log"
grep -q -- "--sd-b PATH" "$log"
grep -q -- "--sd-mode MODE" "$log"
grep -q -- "--megaflash-rom PATH" "$log"
grep -q -- "--megaflash-sd-a PATH" "$log"
grep -q -- "--megaflash-sd-b PATH" "$log"
grep -q -- "--disk-a PATH" "$log"
grep -q -- "--disk-b PATH" "$log"
grep -q -- "--floppy-mode MODE" "$log"
grep -q -- "--ide PATH" "$log"
grep -q -- "--ide-mode MODE" "$log"
grep -q -- "--cassette PATH" "$log"
grep -q -- "--screenshot PATH" "$log"

./1983 --version >"$log"
grep -q '^1983 0\.2\.0 (git ' "$log"

echo "command-line media and mapper tests passed"
