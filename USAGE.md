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
./1983                          # Omega MSX2 using bundled RainBIOS
./1983 --model msx2 --region pal
./1983 --model nms8250 --memory 1024  # NMS 8250 with 1 MiB RAM
./1983 --cart ROMS/game.rom
./1983 --config ./my.conf       # isolated configuration
./1983 --help
```

Fresh configurations select the bundled `omega-msx2` RainBIOS model and
record that complete firmware choice in the per-user `1983.conf`. The bundled
`cbios` model remains available for MSX1 software.

## Command line

```
--model ID                select a machine from the catalogue
--memory KB               RAM in KiB; exact supported size, model dependent
--region ntsc|pal
--bios FILE / --logo FILE explicit C-BIOS main and logo ROMs
--cart FILE              insert a cartridge
--cassette FILE          insert a CAS tape
--disk-a/--disk-b FILE   floppy images (controller model), --floppy-mode access
--sunrise-rom + --ide    Sunrise IDE controller ROM and image, --ide-mode
--scsi-rom + --scsi-disk NCR/Z5380 MSX SCSI ROM and raw image,
                            --scsi-port 30|D0 and --scsi-mode
--sd-mapper-rom + --sd-a/--sd-b      SD Mapper V2, --sd-mode
--megaflash-rom + --megaflash-sd-a/--megaflash-sd-b  MegaFlashROM, --sd-mode
 --cdx2-rom FILE         16 KB ROM or 32 KB dual-ROM CDX-2 EPROM image
 --cdx2-bank 0|1         select its lower/upper 16 KB jumper position
 --rdf600-rom FILE       user-provided RDF600/TDC-600 controller ROM
 --powergraph-v9990       PowerGraph V9990 external video cartridge
 --unapi                   optional openMSXnet host bridge on NMS 8250
 --rs232                   RS-232C interface on ports 80h-87h (PTY /tmp/1983-rs232)
 --rs232-rom PATH          user-provided RS-232C EXTBIO/driver ROM
  --headless --unthrottled --exit-after N   deterministic runs
  --dump-screen-text N      run N frames, print the text screen as ASCII
  --gif-out FILE            capture GIF on startup
  --screenshot              (see Controls / F4)
  ```

Run `./1983 --help` for the complete list.

`--memory` overrides the configured RAM size after the selected model is
resolved, so its position relative to `--model` does not matter. MSX2 and
NMS 8250 machines accept 64, 128, 256, 512, 1024, 2048, or 4096 KiB. MSX1
machines additionally accept 16 and 32 KiB. Unsupported sizes are rejected
rather than rounded down.

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

`1983-models.conf` ships with `omega-msx2`, `cbios`, `msx1`, `msx2`, and
`nms8250` machines.
Open the **F9** overlay and choose **General > Machine**. Catalogue mappings
load exactly as defined and the whole firmware set is validated before the
running machine is replaced.

To add a machine, copy a catalogue section, give it a unique ID/name, and
point its firmware paths at files you are entitled to use (the Git-ignored
`ROMS/` directory is the default place). Optional firmware fields may be left
blank. With **General > Tinker** enabled, the **Advanced > Machine model
editor** provides graphical add/edit/duplicate/delete over the catalogue.
The editor also configures an optional Philips WD2793, its primary/secondary
slot, and matching 16 KiB disk ROM. MSX2 definitions may instead select a
512 KiB **Unified ROM** and its lower/upper 256 KiB JP1 bank. A unified image
provides the complete Omega slot contents, so selecting it clears and disables
the individual BIOS, logo, Sub-ROM, and disk-ROM paths. This means either a
generic MSX/MSX2 definition or the supplied NMS 8250 definition can provide
floppy support.

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

Cartridge-connected devices: the first configured reserves cartridge slot 1,
the second slot 2; reserved cartridge/mapper controls stay visible but unusable.
The optional openMSXnet UNAPI bridge is port-mapped and uses no cartridge
slot — guest software must run openMSXnet v0.9.7's separate `UNAPINET.COM` TSR.

### Microsol CDX-2

**Extensions > CDX-2 FDC** models the external cartridge controller: its
user-provided 16 KiB ROM occupies a cartridge slot and its WD2793 is available
at ports `D0h-D4h`. A 32 KiB 27C256 dump is also accepted: **CDX-2 ROM
switch** reproduces the board's physical A14 jumper and chooses its lower or
upper 16 KiB ROM. Enter selects the image on first connection; Space replaces
it, and Delete disconnects and clears it. `--cdx2-rom FILE` and
`--cdx2-bank 0|1` provide the same startup setup. Bank 0 selects the lower
half, which is the Angeisa firmware in the commonly distributed
`angesia_fast.rom`; bank 1 selects FAST!DiskROM. Firmware is not distributed
with 1983. The inserted floppy must be a raw MSX-formatted disk image.

### RDF600

**Extensions > RDF600 FDC** models the RDF600 cartridge and the Talent
TDC-600 hardware on which it is based. Select a user-provided exact 16 KiB
RDF600/TDC-600 Disk ROM; the cartridge reserves one physical slot and exposes
its TC8566AF-compatible command controller through the original mirrored
memory windows. Space replaces the ROM and Delete disconnects and clears it.
`--rdf600-rom FILE` provides the same startup setup. Firmware is not bundled.
Floppy media remains a conventional raw MSX `.dsk` selected under Media.

### PowerGraph V9990

**Extensions > PowerGraph V9990** connects a GFX9000-compatible external
video cartridge with 512 KiB VRAM. It reserves one physical cartridge slot,
does not require Tinker, and resets the machine when connected or removed.
The adjacent **PowerGraph output** setting defaults to `Auto`: BIOS, BASIC and
DOS remain visible through the internal VDP, then 1983 follows PowerGraph when
V9990 software enables its display. `MSX VDP` and `V9990` force either output,
matching a real dual-monitor or manually switched setup. SYMG9K is the
recommended deterministic guest test: it starts SymbOS with the V9990 display
driver. The V9990 bitmap cursor used by the SymbOS mouse is rendered by 1983;
select `Mouse` for the active joy port and click the display to capture it.

### Sunrise IDE

**Extensions > Sunrise IDE** opens a setup panel: choose a 128 KiB
Sunrise/Nextor controller ROM, optionally a raw IDE image, then Connect.
New images start read-only; Space reopens the panel, **Delete** disconnects
and clears settings. Media owns subsequent image changes and safe ejection.
With Tinker, **Advanced > IDE access mode** switches read-only/read/write.
Images must use complete 512-byte sectors; dirty data flushes on FLUSH CACHE,
replacement, ejection, and shutdown, and host I/O failures block unsafe
ejection. Keep backups of writable images.

### MSX SCSI

**Extensions > MSX SCSI** connects a banked controller ROM and one raw
512-byte-sector disk at target ID 0 by default. 1983 bundles
`BertSCSI-v2-30h-37h.ROM` for the default `30h-37h` range and
`BertSCSI-v1-D0h-D7h.ROM` for the alternate `D0h-D7h` range. Select matching
ROM and port revisions. Media owns subsequent disk insertion and safe
ejection; with Tinker, Advanced owns the explicit SCSI read-only/read/write
mode. The cartridge reserves a physical slot. Its D0 revision is mutually
exclusive with CDX-2 because both decode ports in the `D0h-D7h` range.

The tested BERT SCSI V2.7 ROM boots MSX-DOS2 from its own type-01 FAT12
partition format, not an ordinary Sunrise/Nextor FAT16 image. See
[`SCSI.md`](SCSI.md) and `tools/create-bert-scsi-image.sh` for a reproducible
image recipe. The DOS system files are not distributed.

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
