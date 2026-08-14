# 1983 - Usage guide

This guide covers day-to-day operation: starting machines, media, and
extensions. The top-level overview lives in [`README.md`](README.md); the
keyboard/input reference lives in [`CONTROLS.md`](CONTROLS.md),
configuration in [`CONFIGURATION.md`](CONFIGURATION.md), and implementation
details in [`TECHNICAL.md`](TECHNICAL.md) and
[`DEVELOPMENT.md`](DEVELOPMENT.md).

## Quick start

```sh
make -j4
./1983                          # generic MSX1 using bundled C-BIOS
./1983 --model msx2 --region pal
./1983 --cart ROMS/game.rom
./1983 --config ./my.conf       # isolated configuration
./1983 --help
```

Fresh configurations select the bundled `cbios` model and record that complete
firmware choice in the per-user `1983.conf`.

## Command line

```
--model ID                select a machine from the catalogue
--region ntsc|pal
--bios FILE / --logo FILE explicit C-BIOS main and logo ROMs
--cart FILE              insert a cartridge
--cassette FILE          insert a CAS tape
--disk-a/--disk-b FILE   floppy images (controller model), --floppy-mode access
--sunrise-rom + --ide    Sunrise IDE controller ROM and image, --ide-mode
--sd-mapper-rom + --sd-a/--sd-b      SD Mapper V2, --sd-mode
--megaflash-rom + --megaflash-sd-a/--megaflash-sd-b  MegaFlashROM, --sd-mode
 --cdx2-rom FILE         user-provided Microsol CDX-2 controller ROM
 --unapi                   optional openMSXnet host bridge on NMS 8250
 --rs232                   RS-232C interface on ports 80h-87h (PTY /tmp/1983-rs232)
 --rs232-rom PATH          user-provided RS-232C EXTBIO/driver ROM
 --headless --unthrottled --exit-after N   deterministic runs
 --gif-out FILE            capture GIF on startup
 --screenshot              (see Controls / F4)
 ```

Run `./1983 --help` for the complete list.

### RS-232C and the driver ROM

Enabling **Extensions > RS-232C** (or `--rs232` / `rs232 = true`) turns on the
serial interface itself: the 8251/8254 respond on ports `80h-87h` and the host
side is a PTY at `/tmp/1983-rs232` (Linux/BSD) that you attach with
`picocom /tmp/1983-rs232` or `minicom -D /tmp/1983-rs232`. Guest software that
programs the ports directly works with just this.

The **EXTBIO/driver ROM is only needed for auto-detection** so MSX-BASIC's
`OPEN "COM0:"` finds the interface (EXTBIO function 08h). It is user-provided:
set `rs232_rom = /path/to/rs232.rom` (or `--rs232-rom PATH`) and the ROM is
loaded into a cartridge slot automatically when RS-232C is enabled. Without
it, the port device still works, but `OPEN "COM0:"` auto-detect is
unavailable. The ROM is not bundled with 1983.

## Firmware and machines

`1983-models.conf` ships with `cbios`, `msx1`, `msx2`, and `nms8250` machines.
Open the **F9** overlay and choose **General > Machine**. Catalogue mappings
load exactly as defined and the whole firmware set is validated before the
running machine is replaced.

To add a machine, copy a catalogue section, give it a unique ID/name, and
point its firmware paths at files you are entitled to use (the Git-ignored
`ROMS/` directory is the default place). Optional firmware fields may be left
blank. With **General > Tinker** enabled, the **Advanced > Machine model
editor** provides graphical add/edit/duplicate/delete over the catalogue.
The editor also configures an optional Philips WD2793, its primary/secondary
slot, and matching 16 KiB disk ROM. This means either a generic MSX/MSX2
definition or the supplied NMS 8250 definition can provide floppy support.

See [`BOOT_TARGETS.md`](BOOT_TARGETS.md) for firmware lanes, checkpoints, and
the GeoBench/Nextor targets.

## Media

### Floppy

For a machine model with a configured controller, **Media > Floppy A / B**
inserts a raw MSX `.dsk` image; Delete safely ejects it. Models without a
controller reject floppy insertion cleanly. A controller mapped to primary
slot 1 or 2 reserves the matching cartridge port; an internal MSX2 controller
normally uses expanded slot 3-3 instead.
With Tinker, **Advanced > Floppy access mode** selects read-only or read/write
(read-only is default). Sector writes flush on replacement, ejection, and
shutdown; reset discards only an incomplete transfer. The backend accepts raw
160, 180, 320, 360, 640, and 720 KiB MSX disk images with 512-byte sectors.

### Cassette

**Media > Cassette** inserts a `.cas` image and rewinds it; **R** on the row
rewinds, **Delete** ejects. Playback advances only while guest software turns
on the motor (the Tape LED follows it). Use `RUN"CAS:"` for ASCII, `BLOAD"CAS:",R`
for binary, or `CLOAD` + `RUN` for tokenized BASIC. With Tinker, Advanced
offers Tape Audio Monitor and Tape Visual Monitor (recording is not
implemented yet).

## Extensions

Cartridge-connected devices: the first configured reserves cartridge slot 2,
the second slot 1; reserved cartridge/mapper controls stay visible but unusable.
The optional openMSXnet UNAPI bridge is port-mapped and uses no cartridge
slot — guest software must run openMSXnet v0.9.7's separate `UNAPINET.COM` TSR.

### Microsol CDX-2

**Extensions > CDX-2 FDC** models the external cartridge controller: its
user-provided exact 16 KiB ROM occupies a cartridge slot and its WD2793 is
available at ports `D0h-D4h`. Enter selects the ROM on first connection;
Space replaces it, and Delete disconnects and clears it. `--cdx2-rom FILE`
provides the same startup setup. The firmware is not distributed with 1983.
The inserted floppy must be a raw MSX-formatted disk image.

### Sunrise IDE

**Extensions > Sunrise IDE** opens a setup panel: choose a 128 KiB
Sunrise/Nextor controller ROM, optionally a raw IDE image, then Connect.
New images start read-only; Space reopens the panel, **Delete** disconnects
and clears settings. Media owns subsequent image changes and safe ejection.
With Tinker, **Advanced > IDE access mode** switches read-only/read/write.
Images must use complete 512-byte sectors; dirty data flushes on FLUSH CACHE,
replacement, ejection, and shutdown, and host I/O failures block unsafe
ejection. Keep backups of writable images.

### SD Mapper V2

Setup: choose a 128/256 KiB controller ROM, optionally SD Card A/B, toggle the
cartridge's 512 KiB mapper, Connect. One physical slot is reserved; the device
exposes an expanded slot internally (registers in subslot 0, mapper RAM in
subslot 1). Media owns later card insertion/ejection. With Tinker, Advanced
provides SD access mode, SD mapper 512K RAM, and the driver switch. Two green
leds report SD card traffic. Controller ROMs and cards are user-supplied.

### MegaFlashROM SCC+ SD

Setup keeps three distinct items: an initial flash dump up to 8 MiB, removable
SD Card A/B. The dump seeds a **private writable flash state** under the
configuration directory's `flash/`; guest programming never writes the source
dump. Includes recovery plus multi-mapper subslots, an independent 512 KiB
mapper, MegaSD subslot, SCC-I, and cartridge PSG. Cards/ejection live under
Media with the same SD access mode as SD Mapper V2; the owning cartridge LED
stays yellow. The official `mfrsd.rom` preflash is accepted at its native
8,208,384-byte length (erased area auto-padded).

## Configuration

See [`CONFIGURATION.md`](CONFIGURATION.md).
