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
| Nextor | Disk operating system for machines with a supported storage controller | First mass-storage boot target on the GeoBench NMS 8250 profile |

C-BIOS and a vendor-compatible BIOS/BASIC set are alternatives at the machine
firmware layer. Nextor is not a replacement for that layer: it is a disk
operating system, supplied as a kernel ROM plus files on a guest volume.

## GeoBench MSX2 reference target

The first Nextor target deliberately matches
`../geobench/tools/run_msx.sh`, so GeoBench can be compared between 1983,
openMSX, and physical MSX2 hardware without changing its guest disk:

- Philips NMS 8250, PAL, with its real expanded-slot layout;
- Z80 at 3,579,545 Hz and a V9938 with 128 KB of VRAM;
- the machine's 128 KB internal memory mapper;
- a separate 512 KB external memory-mapper extension by default;
- an emulated Sunrise ATA-IDE cartridge with the unmodified official Nextor
  2.1.1 Sunrise IDE kernel ROM;
- a raw IDE hard-disk image with 512-byte sectors, normally GeoBench's
  32 MiB `QA/GBMSX.IMG`;
- a FAT16 boot volume containing `NEXTOR.SYS`, `COMMAND2.COM`, and GeoBench.

The 512 KB expansion must remain a separate mapper device rather than being
folded into a fictitious 640 KB mapper. The stock 128 KB configuration will
also be testable: it is useful for exposing memory-pressure failures, while
the expansion is the normal GeoBench configuration.

The equivalent independent-reference launch is:

```sh
openmsx -machine Philips_NMS_8250 \
  -ext SunriseIDE_Nextor -ext ram512k -ext unapinet \
  -hda /path/to/GBMSX.IMG
```

This is the GeoBench launcher's default. Its `MSX_RAM=stock` mode omits
`ram512k`, and `MSX_UNAPI=0` omits `unapinet`. The networking extension is
not required for the first disk-boot checkpoint; it will become an optional
device once the base machine and storage path are deterministic.

Nextor can run on MSX1 and can fall back to MSX-DOS 1 mode, but its normal
MSX-DOS 2-compatible mode requires at least 128 KB in the largest memory
mapper. A 64 KB machine can reach an MSX-DOS 1 command prompt when the
corresponding MSX-DOS 1 system files are available. These paths will come
after the NMS 8250 checkpoint.

The Sunrise cartridge is the first controller because it is already used by
GeoBench, official Nextor releases provide a matching kernel ROM, openMSX
provides a useful independent hardware reference, and the 1984 sibling
already contains a small ATA/LBA backend that can be adapted behind an
MSX-specific Sunrise register wrapper.

### Local system ROM contract

1983 will reuse ROMs from the user's openMSX setup without copying them into
the source tree. On Unix-like systems the first search root will be
`~/.openMSX/share/systemroms`; configured additional roots and explicit file
overrides will take precedence. Search roots will be recursive, and known
components will be selected by checksum rather than depending on their
filenames.

The pinned reference set is:

| Component | Size | SHA-1 |
|-----------|-----:|-------|
| NMS 8250 BIOS + BASIC | 32 KiB | `6103b39f1e38d1aa2d84b1c3219c44f1abb5436e` |
| NMS 8250 MSX2 sub-ROM | 16 KiB | `5c1f9c7fb655e43d38e5dd1fcc6b942b2ff68b02` |
| NMS 8250 disk ROM v1.08 | 16 KiB | `dab3e6f36843392665b71b04178aadd8762c6589` |
| Nextor 2.1.1 Sunrise IDE kernel | 128 KiB | `dca824d7b0ddf25c6e87a8098e97ab7489725f57` |

The filenames suggested by openMSX are
`nms8250_basic-bios2.rom`, `nms8250_msx2sub.rom`,
`nms8250_disk.rom`, and `Nextor-2.1.1.SunriseIDE.ROM`, but names
are only hints. A missing-ROM diagnostic must identify the component and
expected checksums and must never silently substitute an incompatible image.

Neither the Philips ROMs nor the Nextor kernel ROM are committed to this
repository. The GeoBench hard-disk image and machine-specific DOS files remain
external test inputs. The project does track the unmodified guest-side
`DOS/NEXTOR.SYS`; all other files under `DOS/` are ignored. A newer Nextor
kernel can be added as a separate compatibility fixture; it must not silently
change this reproducible reference profile.

## Boot checkpoints

Development tests advance through explicit checkpoints:

1. **Reached:** execute C-BIOS 0.29 reset code and reach a stable cartridge
   startup path. At 180 NTSC frames the current fixture reaches `PC=1A65`,
   `SP=F300`, primary-slot register `F0`, and 5,692 non-zero VRAM bytes. The
   same test then verifies C-BIOS launching a synthetic plain cartridge.
2. Execute a supplied MSX1 BIOS and reach the BASIC prompt.
3. **In progress:** the supplied NMS 8250 BIOS, MSX2 sub-ROM, and disk ROM
   load into the expanded-slot layout, the internal mapper is accessible,
   and the CPU-visible V9938 register/palette/128 KB VRAM interface is in
   place. The RP-5C01 RTC now lets firmware enable an MSX2 bitmap mode and
   initialize 32 KiB of VRAM; implement V9938 commands and bitmap rendering
   to turn that state into a visible firmware checkpoint.
4. Enumerate the external 512 KB mapper and an empty Sunrise IDE device from
   the Nextor kernel ROM.
5. Read a partitioned raw disk and reach the BASIC fallback when the system
   files are absent.
6. Load `NEXTOR.SYS` and `COMMAND2.COM` and reach a deterministic command
   prompt.
7. Verify guest writes, reset persistence, FAT12/FAT16 access, and IDE LED
   activity.

Each checkpoint should be scriptable in headless mode and should record the
firmware hashes, machine profile, disk-image hash, CPU milestone, and
framebuffer hash needed to reproduce it.

The reached C-BIOS fixture uses `cbios_main_msx1.rom` with SHA-256
`921d35edf143f1fde8e53570f92f85e05854610d6a5ea76cce881f2f9040cd9c`
and `cbios_logo_msx1.rom` with SHA-256
`8ad88a4653e26bdbd4c38329fe0a115846e9aa0866b0ac1fe1dd2c260c9932b3`.

## Media and configuration contract

The frontend reserves independent selectors for:

- the machine profile and its firmware components;
- recursive system-ROM search roots;
- cartridge slots;
- floppy and cassette media;
- the Nextor kernel ROM;
- the Sunrise IDE raw hard-disk image.

Selecting the Sunrise extension must not silently replace a cartridge or
firmware image. The NMS 8250 profile will explicitly reproduce slot 0
BIOS/BASIC, expanded primary slot 3 with the MSX2 sub-ROM, internal mapper,
and disk controller, plus separately allocated external mapper and Sunrise
devices. Writable hard-disk images must be opened conservatively and write
activity must drive the IDE status LED.

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
of the GPL emulator. The tracked `DOS/NEXTOR.SYS` is kept as an unmodified,
separately licensed guest file. The machine's Nextor kernel ROM remains an
external, user-supplied official release.

The authoritative sources are:

- <https://cbios.sourceforge.net/>
- <https://github.com/Konamiman/Nextor>
- <https://openmsx.org/manual/setup.html#systemroms>
