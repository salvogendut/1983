# 1983 technical overview

This document summarizes the hardware and frontend behavior currently
implemented by 1983. Low-level design notes and source ownership are in
[`DEVELOPMENT.md`](DEVELOPMENT.md).

## Machine architecture

1983 uses a cycle-budgeted Z80 with machine-owned memory and I/O callbacks.
Its address space is divided into four 16 KiB pages selected between four
primary slots through PPI port `0xA8`.

- Slot 0 contains the BIOS and optional C-BIOS logo ROM.
- Slots 1 and 2 are independent cartridge devices.
- Slot 3 contains ordinary MSX RAM or the expanded MSX2 devices.

The Philips NMS 8250 layout implements expanded primary slot 3, its inverted
secondary-slot register at `0xFFFF`, the MSX2 Sub-ROM, 128 KiB internal
mapper RAM, and the disk ROM. Memory-mapper segment registers are exposed at
ports `0xFC` through `0xFF`.

The General RAM control offers power-of-two mapper capacities through
4096 KiB (plus the smaller 16/32 KiB MSX1 layouts). Capacity above 128 KiB is
allocated on demand instead of enlarging the machine structure and host
stack. On MSX1, selecting more than 64 KiB enables the mapper port and
segment behavior needed to make the extra RAM addressable.

The current hardware layouts are:

| Setting | MSX | MSX2 | Philips NMS 8250 |
|---------|-----|------|------------------|
| CPU | Z80 at 3,579,545 Hz | Z80 at 3,579,545 Hz | Z80 at 3,579,545 Hz |
| Default RAM | 64 KiB | 128 KiB | 128 KiB |
| VRAM | 16 KiB | 128 KiB | 128 KiB |
| VDP | TMS9918A/TMS9929A | V9938 | V9938 |
| Expanded slots | No | Yes | Yes |
| Memory mapper | With RAM above 64 KiB | Yes | Yes |
| RTC | No | Yes | Yes |
| Firmware | BIOS | BIOS + Sub-ROM | BIOS + Sub-ROM + disk ROM |

## Machine catalogue

`1983-models.conf` separates user-visible model definitions from compiled
hardware layouts. Each `[model id]` entry accepts:

```ini
[model my-msx2]
name = My MSX2
hardware = msx2
bios = ROMS/my-bios.rom
logo =
subrom = ROMS/my-subrom.rom
disk_rom =
```

`hardware` must currently be `msx1`, `msx2`, or `nms8250`. A catalogue may
contain up to 64 valid entries. Duplicate IDs and unknown hardware layouts
are ignored, relative firmware paths are resolved from the catalogue
directory, and built-in entries remain available if no valid catalogue can
be loaded.

General > Machine enumerates the catalogue rather than a compiled model
count. Complete mappings load immediately. Blank, unavailable, or
wrong-sized required components invoke sequential SDL3 file pickers. BIOS,
optional logo, Sub-ROM, and disk ROM are loaded atomically, so cancellation
or validation failure preserves the previous firmware.

With Tinker enabled, Advanced > Machine model editor provides catalogue
list, add, edit, duplicate, and delete workflows. IDs are restricted to
portable INI-safe characters and must be unique. Non-empty firmware paths
selected or changed in the editor are checked for their required 32 KiB or
16 KiB size. Empty fields remain valid and invoke the normal picker when the
model is selected.

Editor changes are written through a same-directory temporary file and
atomically renamed to the per-user `1983-models.conf`. The saved file is
reloaded before replacing the active in-memory catalogue. This leaves the
last valid catalogue active if writing or parsing fails and avoids modifying
read-only packaged data.

The selected model ID, resolved hardware layout, and per-user firmware
overrides are stored in `1983.conf`. Explicit command-line options take
precedence.

## Input

General exposes a Main Input selection between Joy Port A and Joy Port B,
plus independent Joystick/Mouse device selections for both connectors.
These choices are persisted in `1983.conf`. The first SDL3 gamepad is routed
to Main Input when that connector is set to Joystick. Its D-pad and left
stick drive the four directions; south and east drive triggers A and B.
Hot-plug removal clears the input, and another available gamepad is selected
automatically. A connector set to Mouse remains idle until the MSX mouse
protocol is implemented.

## Cartridges

Both external primary slots support:

- Linear ROMs;
- ASCII8 and ASCII16;
- Konami;
- Konami SCC.

Bank registers reset with the machine, banks wrap safely, and automatic
detection uses conservative mapper-write signatures. Each slot also has a
persistent manual override. The SCC register window is mapped, but SCC audio
is not implemented yet.

The Media overlay can load, eject, and independently configure both
cartridges. Cassette and floppy rows remain explicit stubs. The IDE
hard-disk selector appears only when Sunrise IDE is connected; the Nextor
kernel is cartridge firmware and therefore has no separate Media row.

General > Extra Hardware reveals the Extensions section. Sunrise IDE, SCC,
and MSX-MUSIC are treated as cartridge-connected devices: the first enabled
device reserves cartridge slot 2 and the second reserves slot 1. A third is
refused. Mounting, ejecting, mapper changes, asynchronous picker completion,
and command-line startup all honor the same reservation state. Enabling an
extension ejects and forgets media in the newly reserved slot.

The footer always shows Cartridge I and Cartridge II indicators between
Power and Caps Lock. An occupied ROM slot or a slot owned by Sunrise IDE is
orange. IDE reads use the dedicated Sunrise IDE indicator. The cartridge
renderer also supports an orange/white network-cartridge form, whose white
half reports network access when a network device is added.

## Sunrise IDE and ATA storage

The Sunrise IDE extension is a real cartridge device rather than a generic
ROM mapper. It implements the 128 KiB, eight-bank ROM window, bit-reversed
bank control, IDE overlay enable, 16-bit data latch, task-file register
window, master/slave selection, alternate status, and device soft reset used
by the Sunrise interface. The address decode follows the openMSX 21.0
Sunrise implementation.

Its host-independent ATA backend exposes an LBA-capable device with IDENTIFY,
READ SECTORS, multiple-sector reads, diagnostic/reset commands, geometry
setup, and the feature commands needed by the official Nextor 2.1.1 Sunrise
kernel. Raw images must be a non-empty multiple of 512 bytes. They are opened
read-only; guest write commands return ABRT and cannot modify the host file.
Failed mounts preserve the previously mounted image.

On first activation, Extensions > Sunrise IDE opens a device-specific setup
panel which distinguishes the required 128 KiB controller ROM from its
optional raw disk image. Nothing is connected or reserved until Connect is
chosen and both selected files have been validated. The firmware path is
then retained so later activations are simple disconnect/reconnect toggles;
Delete on the extension forgets it and re-enables setup for replacement.
Media > IDE hard disk owns subsequent mounting and ejection while the
controller is connected. Both paths persist in `1983.conf`; command-line
equivalents are `--sunrise-rom` and `--ide`. Sector reads pulse the
dedicated Sunrise IDE indicator; the owning cartridge LED remains orange to
show physical presence.

The current reference run uses the NMS 8250 BIOS and Sub-ROM without its
internal disk ROM, because that ROM expects the not-yet-implemented WD2793.
The external Sunrise kernel boots the 32 MiB GeoBench FAT16 image through
Nextor to the GeoBench desktop using the stock 128 KiB mapper.

## MSX1 video

The TMS9918/TMS9929 path implements:

- Text, Graphics I, Graphics II, and Multicolour rendering;
- VRAM data and control ports;
- register and status behavior;
- vertical interrupts;
- sprite mode 1.

Sprite mode 1 supports 8×8 and 16×16 patterns, magnification, early clock,
priority, transparency, Y wrapping, four sprites per line, fifth-sprite
status, collision status, and the attribute-list terminator.

MSX1 rendering currently evaluates most state at frame boundaries.
Cycle-level raster changes remain future work.

## V9938 video

The V9938 implementation includes:

- CPU-visible control and status registers;
- programmable palette and indirect register access;
- 128 KiB VRAM with R14 paging and planar SCREEN 6/7 addressing;
- SCREEN 5 through SCREEN 8 bitmap rendering;
- 192/212-line output, display pages, vertical scrolling, palette
  transparency, and SCREEN 8 fixed colors;
- sprite mode 2 in SCREEN 4 through SCREEN 8;
- vertical and R19 horizontal interrupts;
- beam-positioned S#2 VR/HR status;
- progressive scanline rendering.

Sprite mode 2 supports per-line color attributes, EC/CC/IC behavior,
lower-index priority, eight sprites per line, ninth-sprite status,
transparent color, magnification, early clock, collision coordinates, and
the 192/212-line layouts.

All twelve bitmap commands are implemented: POINT, PSET, SRCH, LINE, LMMV,
LMMM, LMCM, LMMC, HMMV, HMMM, YMMM, and HMMC. Autonomous and CPU-transfer
commands advance with the emulated beam, including CE/TR handshakes,
preloaded transfers, logical operations, and CPU priority when both engines
request the same VRAM slot.

CPU VRAM traffic and command operations use measured screen-off, bitmap,
character, and text access schedules. Timed VRAM, palette, backdrop, page,
scroll, and display-enable changes preserve completed rows and affect later
ones. Pixel-level changes within the active scanline are not yet timed.

The timing work is cross-checked against the
[Yamaha V9938 Technical Data Book](https://map.grauw.nl/resources/video/yamaha_v9938.pdf),
the
[V9938 Programmer's Guide](https://ia800409.us.archive.org/25/items/tms9918_guide/V9938-programmers-guide%20insecure.pdf),
and openMSX's measured
[V9938 VRAM timings](https://openmsx.org/vdp-vram-timing/vdp-timing.html)
and [part II](https://openmsx.org/vdp-vram-timing/vdp-timing-2.html).

## Audio and RTC

The host-independent PSG implements AY-3-8910 and YM2149 variants with three
tone channels, the 17-bit noise generator, all envelope shapes, mixer
behavior, fixed and envelope volume curves, and volume-register DAC output.
It runs at MSX bus timing and produces filtered 44.1 kHz signed 16-bit mono
audio through SDL3.

PSG port A reads the selected joystick connector through register 14. Bits
0 through 5 are active-low Up, Down, Left, Right, trigger A, and trigger B.
Register 15 bit 6 selects connector A or B; bits 4 and 5 drive the respective
pin-8 lines, with a high pin returning neutral input. Register 14 bit 6 is
the low international keyboard-layout signal and bit 7 is the high empty
cassette input. PSG port B also drives the Kana LED.

The host-independent machine stores both joystick states separately. Mouse,
real cassette signals, SCC, and MSX-MUSIC audio remain future work.

MSX2 layouts include RP-5C01-compatible latch/data ports at `0xB4` and
`0xB5`, four register banks, hardware masks, control/reset registers,
host-time initialization, and emulated-time calendar advancement.

## Input

The complete 11-row international keyboard matrix is connected through PPI
ports `0xA9` and `0xAA`. It supports modifiers, function and editing keys,
numeric keypad, simultaneous-key rollover, host aliases, and focus-loss
cleanup.

SDL input is positional. The frontend reserves unmodified function keys;
Shift+F1 through Shift+F5 send the guest function keys, Shift+F7 sends
SELECT, and Shift+F8 sends STOP. Alternate national matrices and
model-specific electrical ghosting are not implemented.

The SDL3 controller adapter applies a 16,000-unit analogue dead zone and
normalizes opposing directions to neutral before passing a six-bit state to
the machine core. Both joystick ports, PSG selection, and pin gating remain
independent of SDL and are covered by deterministic tests.

## Frontend

The SDL3 frontend follows the shared 1984/1985 interface:

- resizable 640×480 guest output;
- shortcut footer and MSX LED strip;
- F9 settings overlay;
- fullscreen and integer window scaling;
- screenshots, pause, reset, notifications, and headless execution;
- Power, Caps, Kana, drive, cassette, and IDE status surfaces.

The emulator is kept behind a narrow machine boundary: core hardware does
not depend on SDL and can run in component tests, headless tools, and future
frontends.

## Known major gaps

- Cassette and floppy devices and image formats.
- WD2793 disk controller.
- Writable ATA images, CHS-only edge cases, and additional ATA commands.
- Separate external memory-mapper extensions.
- MSX mouse input.
- Alternate national keyboard layouts.
- SCC and MSX-MUSIC audio.
- Persistent RTC CMOS files.
- Snapshots, debugger/disassembler, and animated capture.
- Within-scanline fetch timing and further compatibility refinement.

See [`ROADMAP.md`](ROADMAP.md) for sequencing and
[`BOOT_TARGETS.md`](BOOT_TARGETS.md) for the storage and firmware milestones.
