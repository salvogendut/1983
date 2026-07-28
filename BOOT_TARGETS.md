# Firmware and boot targets

1983 keeps host emulator code, machine firmware, storage-controller firmware,
and guest operating-system files as separate layers. This matters both for
machine accuracy and for licensing.

## Supported boot lanes

The implementation will support these complementary paths:

| Path | Purpose | Initial expectation |
|------|---------|---------------------|
| C-BIOS | Redistributable out-of-box firmware for cartridge software | Useful for early Z80, slot, VDP, input, and cartridge tests; no BASIC, cassette, or disk support |
| User-supplied MSX BIOS/BASIC | Representative real-machine behaviour | Required for BASIC and for broad software and peripheral compatibility |
| Nextor | Disk operating system for machines with a supported storage controller | First mass-storage boot target on the generic MSX2 profile |

C-BIOS and a vendor-compatible BIOS/BASIC set are alternatives at the machine
firmware layer. Nextor is not a replacement for that layer: it is a disk
operating system, supplied as a kernel ROM plus files on a guest volume.

## First Nextor target

The initial target is:

- Generic MSX2 with at least 128 KB of mapped RAM;
- user-supplied BIOS/BASIC and MSX2 extension ROMs;
- an emulated Sunrise ATA-IDE cartridge;
- the unmodified official Nextor 2.1.4 Sunrise IDE kernel ROM;
- a raw IDE disk image with 512-byte sectors;
- a FAT12 or FAT16 boot volume containing `NEXTOR.SYS` and `COMMAND2.COM`.

Nextor can run on MSX1 and can fall back to MSX-DOS 1 mode, but its normal
MSX-DOS 2-compatible mode requires at least 128 KB in the largest memory
mapper. A 64 KB machine can reach an MSX-DOS 1 command prompt when the
corresponding MSX-DOS 1 system files are available. These paths will come
after the deterministic generic MSX2 checkpoint.

The Sunrise cartridge is the first controller because official Nextor
releases provide a matching kernel ROM, the Nextor getting-started guide uses
it as the emulator example, openMSX provides a useful independent hardware
reference, and the 1984 sibling already contains a small ATA/LBA backend that
can be adapted behind an MSX-specific Sunrise register wrapper.

The first reproducible fixture is
`Nextor-2.1.4.SunriseIDE.ROM` (SHA-256
`4eafcd3a4918da7da98559b2b598d430521d35857f1bf0d2ba6619f8e71c05b2`).
This is the standard Sunrise hardware variant rather than the blueMSX-specific
variant. Updating the fixture is a deliberate compatibility-test change, not
an automatic download of whatever release happens to be newest.

## Boot checkpoints

Development tests should advance through explicit checkpoints:

1. Execute C-BIOS reset code and reach a stable cartridge startup path.
2. Execute a supplied MSX1 BIOS and reach the BASIC prompt.
3. Execute a supplied MSX2 BIOS and extension ROM with a working memory
   mapper.
4. Enumerate an empty Sunrise IDE device from the Nextor kernel ROM.
5. Read a partitioned raw disk and reach the BASIC fallback when the system
   files are absent.
6. Load `NEXTOR.SYS` and `COMMAND2.COM` and reach a deterministic command
   prompt.
7. Verify guest writes, reset persistence, FAT12/FAT16 access, and IDE LED
   activity.

Each checkpoint should be scriptable in headless mode and should record the
firmware hashes, machine profile, disk-image hash, CPU milestone, and
framebuffer hash needed to reproduce it.

## Media and configuration contract

The frontend reserves independent selectors for:

- the machine firmware set;
- cartridge slots;
- floppy and cassette media;
- the Nextor kernel ROM;
- the Sunrise IDE raw hard-disk image.

Selecting the Sunrise extension must not silently replace a cartridge or
firmware image. The slot profile will explicitly describe where each ROM and
device is mapped. Writable hard-disk images must be opened conservatively and
write activity must drive the IDE status LED.

## Distribution and licensing

1983 itself is `GPL-2.0-only`.

C-BIOS is distributed under the 2-clause BSD license and is intended for
redistribution with emulators. Its license notice must accompany any bundled
copy.

Original MSX BIOS, BASIC, extension, and disk ROMs are user-supplied unless a
particular image has explicit redistribution permission.

Nextor is developed separately from 1983. It is based on MSX-DOS 2.31 and has
its own license: unmodified distribution is permitted under its terms, while
commercial use and derivative forks require permission from its copyright
holders. Nextor code or ROM data must not be copied into or relicensed as part
of the GPL emulator. Until packaging has been reviewed against those terms,
1983 will load an external, unmodified official release rather than bundle it.

The authoritative sources are:

- <https://cbios.sourceforge.net/>
- <https://github.com/Konamiman/Nextor>
