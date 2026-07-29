#!/bin/sh
set -eu

log=tests/test-cli-output.tmp
config=tests/test-cli-config.tmp
trap 'rm -f "$log" "$config"' EXIT HUP INT TERM

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
    'sunrise_ide = true' >"$config"
if ./1983 --config "$config" --cart2 missing.rom >"$log" 2>&1; then
    echo "cartridge was accepted in an extension-reserved slot" >&2
    exit 1
fi
grep -q "cartridge slot 2 unavailable: reserved by Sunrise IDE" "$log"

./1983 --help >"$log"
grep -q -- "--model NAME" "$log"
grep -q -- "--models PATH" "$log"
grep -q -- "--cart1 PATH" "$log"
grep -q -- "--cart2 PATH" "$log"
grep -q -- "--mapper1 NAME" "$log"
grep -q -- "--mapper2 NAME" "$log"

echo "command-line mapper tests passed"
