# 1983 - Usage guide

This guide covers day-to-day operation: starting machines, controls, media,
extensions, and configuration. The top-level overview lives in
[`README.md`](README.md); implementation details live in
[`TECHNICAL.md`](TECHNICAL.md) and
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
--disk-a/--disk-b FILE   floppy images (NMS 8250), --floppy-mode access
--sunrise-rom + --ide    Sunrise IDE controller ROM and image, --ide-mode
--sd-mapper-rom + --sd-a/--sd-b      SD Mapper V2, --sd-mode
--megaflash-rom + --megaflash-sd-a/--megaflash-sd-b  MegaFlashROM, --sd-mode
--unapi                   optional openMSXnet host bridge on NMS 8250
--headless --unthrottled --exit-after N   deterministic runs
--gif-out FILE            capture GIF on startup
--screenshot              (see Controls / F4)
```

Run `./1983 --help` for the complete list.

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

See [`BOOT_TARGETS.md`](BOOT_TARGETS.md) for firmware lanes, checkpoints, and
the GeoBench/Nextor targets.

## Controls

| Key | Action |
|-----|--------|
| F4 | Save a PPM screenshot (with a camera-shutter sound) |
| F5 | Reset |
| F6 | Toggle animated GIF recording |
| F8 | Monitor/disassembler placeholder |
| F9 | Open / save-and-close the options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Pause | Pause or resume |
| Ctrl++ / Ctrl+- | Adjust window scale |
| Ctrl+V | Paste host clipboard into the MSX |
| Shift+F1…F5 | MSX F1…F5 |
| Shift+F7 / Shift+F8 | MSX SELECT / STOP |
| Ctrl+Enter | Release captured mouse |

SDL scancodes map positionally to the international MSX keyboard: Left
Ctrl=CTRL, Left Alt=GRAPH, Right Alt=CODE, Right Ctrl=ACC/dead key; both
Shift keys, editing, arrows, and the numeric keypad are supported.

### Overlay

Left/Right change section, Up/Down select, Enter activates, F9 saves, Escape
closes (or offers to discard). In **Extensions**, Enter toggles a device,
Space edits its settings, Delete clears saved settings. **General > Extra
Hardware** reveals Extensions; **General > Tinker** reveals Advanced.

### GIF capture

**F6** or `--gif-out PATH` records an animated GIF. The Advanced section
cycles resolution (720/540/360/240/180), frame rate (25/20/10/5), and encoder
(built-in GIF89a or FFmpeg optimize).

### Mouse and gamepad

With the selected port set to Mouse, click the emulator window to capture
relative movement; Left/Right host buttons map to MSX A/B. Ctrl+Enter, F9,
reset, or losing focus releases capture. With the selected Main Input
connector set to Joystick, the primary SDL3 gamepad drives it while connected.

### Clipboard paste

Ctrl+V replays the host clipboard into the emulated keyboard matrix one key at
a time, verbatim and without an extra Return.

## Media

### Floppy (NMS 8250)

**Media > Floppy A / B** inserts a raw `.dsk` image; Delete safely ejects it.
With Tinker, **Advanced > Floppy access mode** selects read-only or read/write
(read-only is default). Sector writes flush on replacement, ejection, and
shutdown; reset discards only an incomplete transfer. Backend accepts raw 320,
360, 640, and 720 KiB images (extended DSK is not yet supported).

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

User settings live in `~/.config/1983/1983.conf` on Unix and the app-data
directory on Windows; `--config PATH` selects an isolated configuration (RTC
files follow it, so isolated runs get isolated clocks). On Unix,
`--config /dev/null` disables RTC persistence for disposable runs.
`1983.conf.example` documents available settings.

`1983-models.conf` is searched in the user config directory, the current
directory, and the installed app-data directory; `--models PATH` selects a
different catalogue. The local `DOS/` directory is reserved for guest DOS
files and is never distributed.