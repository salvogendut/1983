# 1983 - Configuration

User settings are stored in `~/.config/1983/1983.conf` on Unix-like systems
and in the application-data directory on Windows. `--config PATH` selects an
isolated configuration. [`1983.conf.example`](1983.conf.example) documents the
available settings.

## RTC and CMOS persistence

RTC files follow the selected configuration file, so isolated configurations
also get isolated clocks. On Unix, `--config /dev/null` deliberately disables
RTC persistence for disposable, deterministic runs.

## Machine catalogue

`1983-models.conf` is searched for in the user configuration directory, the
current directory, and the installed application-data directory.
`--models PATH` selects a different catalogue. The graphical machine editor
always writes a per-user catalogue, seeding it with the complete active
catalogue on the first saved edit so installed or repository copies remain
untouched.

Floppy hardware belongs to each catalogue model instead of being inferred
from its display name or compiled hardware layout. A controller-equipped
entry adds these keys alongside its disk ROM:

```ini
[model my-floppy-msx2]
name = My floppy MSX2
hardware = msx2
bios = ROMS/MSX2.ROM
subrom = ROMS/MSX2EXT.ROM
disk_rom = ROMS/my-disk.rom
floppy_controller = philips-wd2793
floppy_primary_slot = 3
floppy_secondary_slot = 3
```

An Omega-style model can replace all four individual firmware paths with one
exact 512 KiB EEPROM image:

```ini
[model my-omega]
name = My Omega MSX2
hardware = msx2
unified_rom = ROMS/my-omega-512k.bin
unified_rom_bank = 0
bios =
logo =
subrom =
disk_rom =
floppy_controller = philips-wd2793
floppy_primary_slot = 3
floppy_secondary_slot = 3
```

Bank `0` selects bytes `00000h-3FFFFh` (JP1 off) and bank `1` selects
`40000h-7FFFFh` (JP1 on). Each bank is four consecutive 64 KiB images for
slot 0, 3-0, 3-1, and 3-3. Selecting a unified ROM in the model editor clears
the individual BIOS, logo, Sub-ROM, and disk-ROM paths; selecting any
individual component clears the unified path. With that machine running,
unshifted F3 flips banks and resets, while Shift+F3 remains the MSX F3 key.

`floppy_controller` is currently `none` or `philips-wd2793`. Primary slot 1
or 2 represents a controller connected through that physical cartridge port
and uses `floppy_secondary_slot = none`; the occupied port is then unavailable
to other cartridges and extensions. On the expanded MSX2 layouts, primary
slot 3 may instead use free secondary slot 1 or 3 for built-in hardware.
Primary slot 0 and the RAM/Sub-ROM subslots are rejected because they would
overlap existing machine devices. A disk ROM without a controller, a
controller without either its 16 KiB disk ROM or a unified image, and
incompatible slot mappings are reported by the model editor.

The NMS 8250 entry's `ROMS/nms8250_disk.rom` is only its supplied default.
To use another compatible ROM, enable Tinker, open **Advanced > Machine model
editor**, edit the model, highlight **FDC Disk ROM**, press Enter, and select
the replacement 16 KiB file. F2 saves that path in the per-user model
catalogue; the ROM may live outside the project's `ROMS` directory.

Older catalogue entries using the NMS 8250 hardware layout and a non-empty
disk ROM are migrated in memory to the Philips controller at slot 3-3. Legacy
NMS entries without a disk ROM remain diskless. Saving the catalogue writes
the explicit keys. The selected model remains the source of this topology;
it is not duplicated in `1983.conf`.

## Guest DOS files

The local `DOS/` directory is reserved for guest DOS files. Its contents,
including `NEXTOR.SYS`, are ignored and are not distributed with 1983.

## MSX SCSI

The `[extensions]` keys `msx_scsi`, `scsi_rom`, and `scsi_target_id` configure
the banked NCR/Z5380 cartridge. `[media]` keys `scsi_image` and
`scsi_image_mode` attach its raw disk as read-only or read-write. Target ID 0
is the default. The ROM path and image chooser keep independent directory
histories. See [`SCSI.md`](SCSI.md) for BERT-specific partition requirements.

## RS-232C serial interface

`rs232 = true` enables the MSX RS-232C interface on ports `80h-87h` (8251
USART + 8254 timer); on Linux/BSD the host side is a PTY at
`/tmp/1983-rs232`. Set `rs232_rom = /path/to/rs232.rom` to provide the
user-supplied MSX-serial driver ROM used for EXTBIO auto-detection
(`OPEN "COM0:"`). The ROM is loaded into a cartridge slot automatically; it
is not bundled with 1983. Without it, the port device still works but BASIC
auto-detect is unavailable.
