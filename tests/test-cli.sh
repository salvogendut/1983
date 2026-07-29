#!/bin/sh
set -eu

log=tests/test-cli-output.tmp
config=tests/test-cli-config.tmp
sunrise=tests/test-cli-sunrise.tmp
cassette=tests/test-cli-cassette.tmp
trap 'rm -f "$log" "$config" "$sunrise" "$cassette"' EXIT HUP INT TERM

if ./1983 --mapper definitely-not-a-mapper >"$log" 2>&1; then
    echo "invalid mapper was accepted" >&2
    exit 1
fi
grep -q "expected auto, linear, ascii8, ascii16, konami" "$log"

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
grep -q -- "--disk-a PATH" "$log"
grep -q -- "--disk-b PATH" "$log"
grep -q -- "--floppy-mode MODE" "$log"
grep -q -- "--ide PATH" "$log"
grep -q -- "--ide-mode MODE" "$log"
grep -q -- "--cassette PATH" "$log"
grep -q -- "--screenshot PATH" "$log"

echo "command-line media and mapper tests passed"
