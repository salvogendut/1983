#!/usr/bin/env python3
"""Build a 512 KiB Omega EEPROM image from RainBIOS components."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import tempfile


ROM_SIZE = 0x80000
BANK_SIZE = 0x40000


def read_exact(path: Path, size: int, description: str) -> bytes:
    data = path.read_bytes()
    if len(data) != size:
        raise SystemExit(
            f"{description} must be exactly {size // 1024} KiB: {path}"
        )
    return data


def main() -> None:
    parser = argparse.ArgumentParser(
        description="cook a two-bank Omega MSX unified ROM"
    )
    parser.add_argument("output", type=Path)
    parser.add_argument("bios", type=Path)
    parser.add_argument("subrom", type=Path)
    parser.add_argument("disk_rom", type=Path)
    args = parser.parse_args()

    bios = read_exact(args.bios, 0x8000, "BIOS")
    subrom = read_exact(args.subrom, 0x4000, "Sub-ROM")
    disk_rom = read_exact(args.disk_rom, 0x4000, "disk ROM")
    image = bytearray([0xFF]) * ROM_SIZE

    # Each 256 KiB jumper bank is four consecutive 64 KiB slot images:
    # slot 0, slot 3-0, slot 3-1, slot 3-3. RainBIOS occupies slot 0;
    # the Sub-ROM starts slot 3-0; the WD2793 disk ROM occupies page 1
    # (4000h-7FFFh) of slot 3-3. Both EEPROM banks intentionally contain
    # the same redistributable default recipe.
    for bank in (0, 1):
        base = bank * BANK_SIZE
        image[base : base + len(bios)] = bios
        image[base + 0x10000 : base + 0x10000 + len(subrom)] = subrom
        image[base + 0x34000 : base + 0x34000 + len(disk_rom)] = disk_rom

    args.output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=args.output.name + ".", dir=args.output.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as temporary:
            temporary.write(image)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, 0o644)
        os.replace(temporary_name, args.output)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


if __name__ == "__main__":
    main()
