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
| Nextor | Disk operating system for machines with a supported storage controller | Boots through Sunrise IDE on the GeoBench NMS 8250 target and through SD Mapper V2 on MSX1; MegaFlashROM provides an additional user-supplied preflash lane |

C-BIOS and a vendor-compatible BIOS/BASIC set are alternatives at the machine
firmware layer. Nextor is not a replacement for that layer: it is a disk
operating system, supplied as a kernel ROM plus files on a guest volume.

## Running current firmware

The local `ROMS/` directory is ignored by Git and is available for
user-supplied firmware, cartridges, and diagnostics. These local files are
ignored; the only committed exceptions are the redistributable C-BIOS MSX1
main and logo ROMs and their upstream license.

### C-BIOS

1983 includes C-BIOS 0.29 from the
[C-BIOS project](https://cbios.sourceforge.net/). Select **C-BIOS MSX** in
the Machine menu, or start the generic 60 Hz MSX machine explicitly with:

```sh
./1983 --region ntsc \
  --bios ROMS/cbios_main_msx1.rom \
  --logo ROMS/cbios_logo_msx1.rom
```

Add a cartridge with `--cart`, or mount it through the Media overlay:

```sh
./1983 --region ntsc \
  --bios ROMS/cbios_main_msx1.rom \
  --logo ROMS/cbios_logo_msx1.rom \
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
slot and internal mapper layout. Its Philips memory-mapped WD2793 now boots
conventional raw DSK images through Floppy A:

```sh
./1983 --model nms8250 --region pal \
  --disk-a /path/to/software.dsk --floppy-mode read-only
```

Enable Extra Hardware and use **Extensions > Second floppy** to add Floppy B
to Media. Both drives support explicit read-only/read/write policy and safe
ejection.

### Graphical floppy workflow

1. Select **General > Machine > Philips NMS 8250** and provide its BIOS,
   Sub-ROM, and disk ROM through the machine catalogue.
2. Open **Media > Floppy A** and select a conventional raw `.dsk` image.
3. Press F5 to reset. The internal disk ROM will boot the inserted image when
   it is bootable; the Floppy A LED reports sector activity.
4. For a second drive, enable **General > Extra Hardware**, then
   **Extensions > Second floppy**. **Media > Floppy B** appears immediately
   and has its own selector and activity LED.
5. Read-only is the safe default for both drives. To permit guest writes,
   select **Advanced > Floppy access mode > Read/write**. Delete on a Media
   row safely ejects that image.

The second-drive setting controls both the emulated Philips drive-select
target and the presence of the Floppy B Media row. Disabling it safely ejects
Floppy B but retains its configured path, so enabling it again can restore the
same image. The access mode is shared by A and B and changes mounted images
conservatively: if either image cannot be reopened, the previous usable mode
is retained where possible and the failure is reported.

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
that configuration. Space reopens its settings without toggling it; Delete
safely disconnects and clears the saved controller and disk configuration.
**Media > IDE hard disk** handles later disk
changes while the controller is connected. Images must be non-empty multiples
of 512 bytes and default to read-only; Advanced > IDE access mode can
explicitly switch them to read/write.

The Sunrise reference command deliberately suppresses the internal disk ROM
so the external controller owns boot.

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

### SD Mapper V2 and Nextor

The [MSX SD Mapper V2](https://github.com/fbelavenuto/msxsdmapperv2) is a
single cartridge containing a dual-SD controller and an optional independent
512 KiB memory mapper. Enable **General > Extra Hardware**, then choose
**Extensions > SD Mapper V2**. Its guided setup asks separately for:

1. the 128 or 256 KiB controller ROM;
2. optional raw images for SD Card A and SD Card B;
3. the 512 KiB mapper switch;
4. the primary or alternate driver half of a 256 KiB ROM.

Connect reserves one physical cartridge slot. Later card changes and safe
ejection live under Media. With Tinker enabled, Advanced owns the shared SD
read-only/read-write policy and both hardware switches. Images must be
non-empty multiples of 512 bytes and default to read-only.
Space reopens the complete setup, while Delete safely disconnects and clears
the saved controller, card, mapper, and driver settings.

Release 1.1.0 with Nextor 2.1.2 provides `SDXC110.ROM`. Its official
firmware and guest files remain local; 1983 does not redistribute them. A
reference MSX1 launch is:

```sh
./1983 --model msx1 \
  --sd-mapper-rom /path/to/SDXC110.ROM \
  --sd-a /path/to/card.img --sd-mode read-only
```

The command-line implementation also accepts `--sd-b`. The reference
firmware has been booted in 1983 from a FAT16 card containing the official
Nextor 2.1.2 system files and reaches a clean `A:\` prompt. The same
controller also boots `../geobench/QA/GBMSX.IMG` on the NMS 8250 profile and
reaches the GeoBench desktop; at the 2,501-frame inspection point it has
performed SD reads, populated 12,293 VRAM bytes, and left the external mapper
at `03,02,01,00`.

### MegaFlashROM SCC+ SD

Enable **General > Extra Hardware**, then choose
**Extensions > MegaFlashROM**. Its setup asks separately for an initial
cartridge flash image up to 8 MiB and optional raw images for SD Card A and SD
Card B. Connect reserves one physical cartridge slot; later card changes and
safe ejection live under Media. The shared Advanced > SD access mode controls
the two removable cards. Space reopens the setup; Delete safely disconnects
and clears the saved initial-image and card settings.

The initial dump is read-only seed material. 1983 creates a private writable
flash state under the active configuration directory and loads that state on
later runs, so flashing software cannot alter the dump. A corrupt state is
rejected and a host flush failure blocks unsafe disconnection. Selecting a
different initial image atomically reseeds the private state. The initial dump,
its flashed software, and SD images are not distributed by 1983.
The current official preflash is available from
[MSX Cartridge Shop](https://www.msxcartridgeshop.com/bin/mfrsd.zip);
its `mfrsd.rom` has SHA-1
`1621f623b834dc57cb2983f30b36bcc3ac56cafd`.

```sh
./1983 --model msx1 \
  --megaflash-rom /path/to/mfrsd.rom \
  --megaflash-sd-a /path/to/card.img --sd-mode read-only
```

The device includes its recovery, MegaFlash, 512 KiB mapper, and MegaSD
subslots plus SCC-I and cartridge PSG audio. Component tests use a synthetic
8 MiB flash image. The official preflash has also booted its internal Nextor
2.10 ROM disk to an `A:\` prompt in 1983. The optional full-system checkpoint
below repeats that run with local firmware; a card image is optional.

### Optional firmware tests

```sh
MSX_CBIOS_DIR=ROMS make check
MSX_DIAG_BIOS_ROM=../msx-diag/msxdiag.rom make check
MSX_NMS8250_DIR=ROMS make check
MSX_NMS8250_DIR=ROMS MSX_DIAG_ROM=ROMS/diag.rom make check
MSX_NMS8250_DIR=ROMS \
MSX_NMS8250_DSK=/path/to/software.dsk \
make check
MSX_NMS8250_DIR=ROMS \
MSX_NEXTOR_SUNRISE_ROM=/path/to/Nextor-2.1.1.SunriseIDE.ROM \
MSX_NEXTOR_IDE_IMAGE=/path/to/GBMSX.IMG \
make check
MSX_SD_MAPPER_BIOS_ROM=/path/to/MSX.ROM \
MSX_SD_MAPPER_ROM=/path/to/SDXC110.ROM \
MSX_SD_MAPPER_IMAGE=/path/to/card.img \
make check
MSX_MEGAFLASH_BIOS_ROM=/path/to/MSX.ROM \
MSX_MEGAFLASH_ROM=/path/to/mfrsd.rom \
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
- a separate 512 KB external memory-mapper extension, now supplied by the
  SD Mapper V2 composite cartridge;
- an emulated Sunrise ATA-IDE cartridge with the unmodified official Nextor
  2.1.1 Sunrise IDE kernel ROM;
- a raw IDE hard-disk image with 512-byte sectors, normally GeoBench's
  32 MiB `QA/GBMSX.IMG`;
- a FAT16 boot volume containing `NEXTOR.SYS`, `COMMAND2.COM`, and GeoBench.

The 512 KB expansion remains a separate mapper device rather than being
folded into a fictitious 640 KB mapper. SD Mapper V2 provides that independent
mapper in its expanded cartridge subslot. The established Sunrise checkpoint
still uses the stock 128 KB configuration and reaches the GeoBench desktop;
running Sunrise and SD Mapper V2 together is the remaining exact-match
checkpoint for the launcher's normal storage configuration.

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
external test inputs. All guest-side files under `DOS/`, including
`NEXTOR.SYS`, are local and ignored. A newer Nextor kernel can be added as a
separate compatibility fixture; it must not silently change this reproducible
reference profile.

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
   are covered independently. The native disk-ROM path also boots the local
   720 KiB `Mahjong Kyo Special` raw DSK
   (`SHA-256 95d94e18f71b3106d8a41207e717fbf5da6380e1e58f595e76836771445b483d`)
   to its title screen; the image remains user-supplied and untracked.
4. **Reached independently:** the official Nextor kernel ROM enumerates the
   Sunrise cartridge and its ATA master on the stock NMS 8250 mapper. The SD
   Mapper V2 also exposes its own 512 KiB mapper through expanded subslot 1
   and combines its mapper-port response correctly with an internal mapper.
5. **Reached for the reference image:** the ATA backend identifies the raw
   device, performs LBA and multiple-sector reads, and traverses the
   partitioned 32 MiB FAT16 image. The missing-system-file BASIC fallback
   still needs its own fixture.
6. **Reached beyond the prompt:** Nextor loads the system files from the
   GeoBench image and executes its startup, reaching the GeoBench desktop.
7. **Reached:** reset preserves mounted ATA and floppy images, completed
   read/write sectors flush safely, partial transfers do not corrupt host
   media, host I/O errors block unsafe ejection, and activity pulses the
   appropriate IDE, SD, or floppy LED.
8. **Reached:** the official `SDXC110.ROM` from SD Mapper V2 release 1.1.0
   initializes SPI storage, loads Nextor 2.1.2 from a FAT16 card image, and
   reaches an `A:\` prompt on a PAL MSX1. On the NMS 8250 it boots the
   GeoBench image to its graphical desktop. Component tests cover both card
   sockets, firmware banking, mapper switches and ports, SDHC/SDSC addressing,
   single/multiple block reads and writes, reset, flush, error, and
   safe-ejection behavior.
9. **Reached:** MegaFlashROM SCC+
   SD exposes four subslots, all five MegaFlash mapper families, protected
   M29W640GB command programming, the 512 KiB mapper, dual MegaSD sockets,
   SCC-I, and cartridge PSG. Persistent flash and SD media reject corruption,
   retain dirty state after failed flushes, and block unsafe ejection. The
   official `mfrsd.rom` preflash boots its internal Nextor 2.10 ROM disk to an
   `A:\` prompt. At 1,200 PAL frames the reference run reaches `PC=0D87`,
   mapper registers `03,02,01,00`, 4,226 non-zero VRAM bytes, and framebuffer
   hash `D2B8FADA1DF43F93`; `NEXTOR.SYS` is present in the name table. This
   reference used an MSX1 BIOS with SHA-256
   `999564a371dd2fdf7fbe8d853e82a68d557c27b7d87417639b2fa17704b83f78`
   and the official preflash with SHA-256
   `54d92573bf88b699b6f15d82f497d268a61a9d491b71aa84fd78d862a4561065`.

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

The SD Mapper V2 checkpoint accepts user-selected BIOS, controller ROM, and
card image paths through the three `MSX_SD_MAPPER_*` variables shown above.
It runs for 900 PAL frames and requires controller connection, card activity,
the enabled 512 KiB mapper, sustained CPU execution, and populated video
state. It reports the CPU and framebuffer state without pinning them because
valid machine BIOS and card contents differ.

## Media and configuration contract

The frontend provides an editable catalogue-backed machine selector and
independent, persistent selectors for both external cartridge slots. It
also provides:

- a guided Sunrise IDE controller setup under Extensions;
- a raw IDE hard-disk selector under Media while Sunrise is connected;
- a guided SD Mapper V2 setup under Extensions and SD Card A/B selectors
  under Media while it is connected;
- a guided MegaFlashROM SCC+ SD setup under Extensions, with an immutable
  initial flash source, private persistent flash state, and its own SD Card
  A/B Media selectors;
- a persistent standard MSX CAS cassette selector and transport under Media;
- a persistent Floppy A selector for the NMS 8250;
- an Advanced second-floppy switch which conditionally adds Floppy B.

Each cartridge selector opens the shared SDL3 file-dialog workflow and has an
adjacent `auto`/manual mapper selector. Delete ejects the selected cartridge.
In Extensions, Enter enables or disables, Space edits configurable settings,
and Delete safely disconnects and clears those settings.
General > Machine enumerates `1983-models.conf`, loads complete mappings
directly and never opens firmware file dialogs. Definitions are composed in
Advanced > Machine model editor, then validated and applied atomically by the
selector. Sunrise setup validates its controller ROM and optional disk before
reserving a cartridge slot; canceling it leaves the live machine unchanged.
Sunrise ROM and disk paths persist in `1983.conf`;
mounting is conservative and a failed replacement leaves the previous image
active. Cassette mounts are also conservative, persist in `1983.conf`, and
expose rewind and eject controls. Floppy mounts follow the same conservative
replacement and safe-ejection policy; paths, the shared access mode, and
second-drive state persist independently.

SD Mapper setup applies the same atomic workflow to its controller ROM and
both removable card images. Firmware, card paths, mapper and driver switches,
and access mode persist independently. Failed card replacement preserves the
previous mount, and an I/O error blocks unsafe ejection.
MegaFlashROM setup validates an initial image up to 8 MiB, seeds private
writable state without modifying that source, and applies the same
conservative card lifetime to both MegaSD sockets.

Selecting the Sunrise extension must not silently replace a cartridge or
firmware image. It reserves a physical cartridge slot through the same
ownership policy as every cartridge-connected extension. SD Mapper V2 does
the same while expanding only its own assigned primary slot, as does
MegaFlashROM SCC+ SD. The NMS 8250
profile reproduces slot 0 BIOS/BASIC and expanded primary slot 3 with the
MSX2 Sub-ROM, internal mapper, and disk ROM; Sunrise remains an independent
external cartridge. SD Mapper RAM remains an independent external mapper.

Hard-disk, SD-card, and floppy images default to read-only and require an explicit
Advanced-menu choice for read/write access. Completed writes are flushed on
guest flush commands where applicable, replacement, ejection, and shutdown.
Read/write activity drives the dedicated IDE, SD, or floppy status LED while
the owning cartridge LED remains monochrome orange.

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
of the GPL emulator. `DOS/NEXTOR.SYS` and the machine's Nextor kernel ROM
remain external, user-supplied official-release files.

The authoritative sources are:

- <https://cbios.sourceforge.net/>
- <https://github.com/Konamiman/Nextor>
- <https://openmsx.org/manual/setup.html#systemroms>
