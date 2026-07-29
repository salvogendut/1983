# Firmware and boot targets

1983 keeps host emulator code, machine firmware, storage-controller firmware,
and guest operating-system files as separate layers. This matters both for
machine accuracy and for licensing.

## Supported boot lanes

The implementation supports these complementary paths:

| Path | Purpose | Initial expectation |
|------|---------|---------------------|
| C-BIOS | Redistributable out-of-box firmware for cartridge software | Useful for early Z80, slot, VDP, input, and cartridge tests; no BASIC, cassette, or disk support |
| User-supplied MSX BIOS/BASIC | Representative real-machine behaviour | Required for BASIC and for broad software and peripheral compatibility |
| Nextor | Disk operating system for machines with a supported storage controller | First mass-storage boot target on the GeoBench NMS 8250 profile |

C-BIOS and a vendor-compatible BIOS/BASIC set are alternatives at the machine
firmware layer. Nextor is not a replacement for that layer: it is a disk
operating system, supplied as a kernel ROM plus files on a guest volume.

## Running current firmware

The local `ROMS/` directory is ignored by Git and is available for
user-supplied firmware, cartridges, and diagnostics. Nothing placed there is
included in commits or release archives.

### C-BIOS

Download C-BIOS from the
[C-BIOS project](https://cbios.sourceforge.net/) and start the generic
60 Hz MSX machine with:

```sh
./1983 --region ntsc \
  --bios /path/to/cbios_main_msx1.rom \
  --logo /path/to/cbios_logo_msx1.rom
```

Add a cartridge with `--cart`, or mount it through the Media overlay:

```sh
./1983 --region ntsc \
  --bios /path/to/cbios_main_msx1.rom \
  --logo /path/to/cbios_logo_msx1.rom \
  --cart /path/to/game.rom
```

C-BIOS runs cartridge software but does not provide BASIC, cassette, or disk
services.

### MSX2 and Philips NMS 8250

With the firmware names referenced by `1983-models.conf` placed in `ROMS/`,
start the supplied layouts with:

```sh
./1983 --model msx2 --region pal
./1983 --model nms8250 --region pal
```

The NMS 8250 BIOS, Sub-ROM, and disk ROM reproduce the implemented expanded
slot and internal mapper layout. The WD2793 controller is not implemented
yet, so the disk ROM does not currently provide disk access.

Boot the local diagnostic cartridge with:

```sh
./1983 --model msx2 --region pal --cart ROMS/diag.rom
```

The `msxdiag.rom` built by sibling project `../msx-diag` is a replacement
MSX1 BIOS rather than a cartridge:

```sh
make -C ../msx-diag
./1983 --model msx1 --region pal --bios ../msx-diag/msxdiag.rom
```

### Sunrise IDE and Nextor

Enable **General > Extra Hardware** and choose **Extensions > Sunrise IDE**.
On first activation, its setup panel asks for the required 128 KiB official
Sunrise Nextor controller ROM and an optional IDE disk image. Choose Connect
after making the selections. Later activations simply disconnect or reconnect
that configuration; Delete forgets the stored controller firmware and opens
the setup path again next time. **Media > IDE hard disk** handles later disk
changes while the controller is connected. Images must be non-empty multiples
of 512 bytes and are mounted read-only.

The current NMS 8250 reference command deliberately suppresses the internal
disk ROM: that firmware expects a WD2793, while the boot device here is the
external Sunrise cartridge.

```sh
./1983 --model nms8250 --disk-rom "" \
  --sunrise-rom /path/to/Nextor-2.1.1.SunriseIDE.ROM \
  --ide /path/to/GBMSX.IMG --ide-mode read-only
```

For a headless inspection run:

```sh
./1983 --model nms8250 --disk-rom "" \
  --sunrise-rom /path/to/Nextor-2.1.1.SunriseIDE.ROM \
  --ide /path/to/GBMSX.IMG --ide-mode read-only \
  --headless --unthrottled --exit-after 2000 --dump-state \
  --screenshot /tmp/1983-nextor.ppm
```

### Optional firmware tests

```sh
MSX_CBIOS_DIR=/path/to/cbios make check
MSX_DIAG_BIOS_ROM=../msx-diag/msxdiag.rom make check
MSX_NMS8250_DIR=ROMS make check
MSX_NMS8250_DIR=ROMS MSX_DIAG_ROM=ROMS/diag.rom make check
MSX_NMS8250_DIR=ROMS \
MSX_NEXTOR_SUNRISE_ROM=/path/to/Nextor-2.1.1.SunriseIDE.ROM \
MSX_NEXTOR_IDE_IMAGE=/path/to/GBMSX.IMG \
make check
```

The corresponding CPU, VRAM, and diagnostic-menu milestones are documented
under Boot checkpoints below.

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
folded into a fictitious 640 KB mapper. The implemented checkpoint currently
uses the stock 128 KB configuration and reaches the GeoBench desktop. The
separate expansion is still required to match the launcher's normal
configuration exactly.

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
after the stock NMS 8250 checkpoint.

The Sunrise cartridge is the first controller because it is already used by
GeoBench, official Nextor releases provide a matching kernel ROM, openMSX
provides a useful independent hardware reference, and the 1984 sibling
provided useful ATA/LBA design precedent. The implemented code keeps the
host-independent ATA task file behind an MSX-specific Sunrise register
wrapper.

### Local system ROM contract

1983 reuses user-supplied ROMs without copying them into the source tree.
`1983-models.conf` currently maps each selectable model to explicit BIOS,
logo, Sub-ROM, and disk-ROM paths; those paths can point into an existing
openMSX ROM pool or the ignored local `ROMS/` directory. The graphical model
editor can maintain those explicit paths. A later discovery pass can search
roots such as `~/.openMSX/share/systemroms` recursively and select known
contents by checksum rather than filename.

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
   same test then verifies C-BIOS launching a synthetic linear cartridge.
   A second synthetic checkpoint executes from an ASCII8 cartridge, changes
   a mapper register through the primary-slot bus, and reads the selected bank.
   An
   independent optional fixture boots the GPL-3.0 `msxdiag.rom` built by
   `../msx-diag` directly as an MSX1 replacement BIOS. At 300 PAL frames it
   has completed its VRAM and RAM checks, reaches the menu key scanner with
   667 non-zero VRAM bytes, and exposes the expected menu strings in Text
   mode. Set `MSX_DIAG_BIOS_ROM` to enable that checkpoint; neither its source
   nor generated ROM is copied into 1983.
2. Execute a supplied MSX1 BIOS and reach the BASIC prompt.
3. **Reached:** the supplied NMS 8250 BIOS, MSX2 sub-ROM, and disk ROM load
   into the expanded-slot layout, the internal mapper is accessible, and the
   RP-5C01 RTC lets firmware initialize the V9938. At 200 PAL frames the
   current fixture reaches `PC=041A`, enables a 512x192 SCREEN 6 display, and
   has 28,346 non-zero VRAM bytes while drawing the MSX2 startup mark. The
   firmware then leaves the Sub-ROM and launches a plain cartridge.
   With the diagnostic ROM supplied through `MSX_DIAG_ROM`, the 1,500-frame
   checkpoint reaches its blue menu at `PC=468C`, restores status selection
   to S#0, and displays `MSX DIAGNOSTICS`. SCREEN 5-8 bitmap layouts, all
   twelve V9938 commands, retrace transitions, and interrupt acknowledgement
   are covered independently.
4. **Partially reached:** the official Nextor kernel ROM enumerates the
   Sunrise cartridge and its ATA master on the stock NMS 8250 mapper. The
   separate external 512 KiB mapper remains to be implemented.
5. **Reached for the reference image:** the ATA backend identifies the raw
   device, performs LBA and multiple-sector reads, and traverses the
   partitioned 32 MiB FAT16 image. The missing-system-file BASIC fallback
   still needs its own fixture.
6. **Reached beyond the prompt:** Nextor loads the system files from the
   GeoBench image and executes its startup, reaching the GeoBench desktop.
7. **Partially reached:** reset preserves the mounted read-only device and
   disk reads pulse the dedicated IDE LED. Guest writes
   deliberately return ATA ABRT; writable media and FAT12 coverage remain.

Each checkpoint should be scriptable in headless mode and should record the
firmware hashes, machine profile, disk-image hash, CPU milestone, and
framebuffer hash needed to reproduce it.

The reached C-BIOS fixture uses `cbios_main_msx1.rom` with SHA-256
`921d35edf143f1fde8e53570f92f85e05854610d6a5ea76cce881f2f9040cd9c`
and `cbios_logo_msx1.rom` with SHA-256
`8ad88a4653e26bdbd4c38329fe0a115846e9aa0866b0ac1fe1dd2c260c9932b3`.

The reached Nextor fixture uses the pinned ROMs above and
`../geobench/QA/GBMSX.IMG` with SHA-256
`f7580fafdf031a429795b2e2fb3c540efb025b233064827246103e39768302b4`.
The test pins the RTC to 1983-01-01 00:00:00 for repeatability. At 2,001 PAL
frames it reaches `PC=82A6`, `SP=D8F8`,
primary slot `FF`, secondary slot `AA`, mapper registers `03,02,01,00`,
8,596 non-zero VRAM bytes, VDP R#0=`0A`, and R#1=`62`. Its rendered VDP
framebuffer has 64-bit FNV-1a hash `7FD8AF872D7E64F1`.

## Media and configuration contract

The frontend provides an editable catalogue-backed machine selector and
independent, persistent selectors for both external cartridge slots. It
also provides:

- a guided Sunrise IDE controller setup under Extensions;
- a raw IDE hard-disk selector under Media while Sunrise is connected;
- a persistent standard MSX CAS cassette selector and transport under Media;
- explicit floppy placeholders for their future devices.

Each cartridge selector opens the shared SDL3 file-dialog workflow and has an
adjacent `auto`/manual mapper selector. Delete ejects the selected cartridge.
General > Machine enumerates `1983-models.conf`, loads complete mappings
directly, and opens sequential file dialogs for missing required components.
The firmware set is applied atomically. Sunrise setup validates its controller
ROM and optional disk before reserving a cartridge slot; canceling it leaves
the live machine unchanged. Sunrise ROM and disk paths persist in `1983.conf`;
mounting is conservative and a failed replacement leaves the previous image
active. Cassette mounts are also conservative, persist in `1983.conf`, and
expose rewind and eject controls. Floppy selectors remain explicit stubs
until their devices are implemented.

Selecting the Sunrise extension must not silently replace a cartridge or
firmware image. It reserves a physical cartridge slot through the same
ownership policy as every cartridge-connected extension. The NMS 8250
profile reproduces slot 0 BIOS/BASIC and expanded primary slot 3 with the
MSX2 Sub-ROM, internal mapper, and disk ROM; Sunrise remains an independent
external cartridge. The separate external mapper is future work.

Hard-disk images are currently opened read-only and guest write commands
abort. Read activity drives the dedicated IDE status LED while the owning
cartridge LED remains monochrome orange. Writable mode will require an
explicit user choice and stronger persistence/error tests before it is
enabled.

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
