# 1983 development notes

This document records source ownership, implementation boundaries, hardware
assumptions, and verification workflows. It is intentionally narrower than a
complete MSX hardware specification.

## Source layout

| Files | Responsibility |
|-------|----------------|
| `src/main.c` | Process lifetime, command line, SDL event loop, and shared function-key bindings |
| `src/ata.*` | Host-independent ATA task file, IDENTIFY/read/write/flush commands, and safe raw-image lifetime |
| `src/floppy.*` | Host-independent raw MSX DSK geometry, I/O, access mode, flush, and safe image lifetime |
| `src/cartridge.*` | Host-independent cartridge image ownership, mapper detection, bank registers, and standard SCC integration |
| `src/cassette.*` | Host-independent MSX CAS parser, type detection, waveform synthesis, motor-controlled transport, comparator, and monitor signal |
| `src/audio.*` | SDL3 audio-stream lifetime and host sample submission |
| `src/config.*` | Defaults, normalization, persistent settings, and platform-specific configuration path |
| `src/display.*` | SDL window and renderer, fixed logical canvas, framebuffer presentation, footer, and screenshots |
| `src/gamepad.*` | Primary SDL3 gamepad discovery, hotplug lifetime, polling, dead zone, and MSX joystick mapping |
| `src/kbd.*` | SDL scancode translation and shared frontend/guest function-key routing |
| `src/paste.*` | Host-independent clipboard-text queue and international MSX matrix injection |
| `src/overlay.*` | F9 options workflow and live application of frontend and machine-profile settings |
| `src/leds.*` | Shared bottom status strip and MSX-specific indicator definitions |
| `src/megaflash.*` | MegaFlashROM SCC+ SD expanded subslots, flash/persistence, mappers, MegaSD, SCC-I, and cartridge PSG |
| `src/models.*` | Editable machine catalogue parsing, firmware-path resolution, and built-in fallback entries |
| `src/notify.*` | On-screen and console notifications |
| `src/ui.*` | Small renderer primitives used by the frontend |
| `src/msx.*` | Machine profiles, slot-aware memory and I/O bus, keyboard/joystick/mouse protocols, ROM loading, and frame scheduler |
| `src/psg.*` | Host-independent AY-3-8910/YM2149 tone, noise, envelope, mixer, and sample generation |
| `src/rtc.*` | Host-independent RP-5C01 registers, test modes, calendar, validated CMOS serialization, and atomic host persistence |
| `src/scc.*` | Host-independent SCC/SCC-I waveform memory, registers, modes, and sample generation |
| `src/sdcard.*` | Host-independent SPI SD command state, raw-card image lifetime, complete-sector writes, flush, errors, and activity |
| `src/sd_mapper.*` | SD Mapper V2 expanded-slot, firmware banking, dual-card registers, timer, and independent 512 KiB mapper |
| `src/sunrise.*` | Sunrise IDE cartridge ROM banking, address decode, 16-bit data latch, and ATA bridge |
| `src/tc8566.*` | RDF600/TDC-600-compatible command/result FDC phases backed by raw MSX floppy images |
| `src/unapinet.*` | openMSXnet v1 port protocol, bounded host TCP/UDP sockets, asynchronous DNS, and network lifecycle |
| `src/z80.*` | Sibling Z80 core and host-independent bus callback contract |
| `src/vdp.*` | TMS9918/TMS9929 renderer plus the V9938 register, palette, beam status, 128 KB VRAM, bitmap, sprite-mode-2, and command engine |
| `src/wd2793.*` | Philips memory-mapped WD2793 registers, commands, drive selection, IRQ/DRQ, and transfer state |
| `diagnostics/test_ata.c` | ATA identify, sector reads, errors, reset, activity, and conservative mount checks |
| `diagnostics/test_floppy.c` | Raw MSX DSK geometry, read/write, failed replacement, flush failure, and safe ejection |
| `diagnostics/test_wd2793.c` | Commands, register mirrors, dual-drive selection, IRQ/DRQ, sector transfers, reset, and write protection |
| `diagnostics/test_tc8566.c` | TC8566AF status, seek/sense, read/write, format, memory mirrors, and write protection |
| `diagnostics/test_sunrise.c` | Sunrise banking, overlay decode, data latch, master/slave, soft reset, and disk lifetime |
| `diagnostics/test_msx.c` | Profiles, slots, CPU execution, device ports, interrupt acknowledgement, and optional C-BIOS/MSX-DIAG/NMS 8250/Nextor boot checks |
| `diagnostics/test_cartridge.c` | Linear, ASCII8/16, Konami, Konami SCC, detection, bank wrapping, reset, and eject checks |
| `diagnostics/test_cassette.c` | CAS type and command detection, waveform layout and monitor sampling, emulated-time transport, reset, rewind, eject, and conservative mounts |
| `diagnostics/test_config.c` | Persistent cartridge, cassette, mapper, extension, and Joy Port settings |
| `diagnostics/test_gamepad.c` | SDL-independent direction, trigger, dead-zone, and opposing-input mapping |
| `diagnostics/test_models.c` | Machine-catalogue parsing, hardware mapping, relative paths, and invalid-entry filtering |
| `diagnostics/test_overlay.c` | Overlay navigation, fixed 1.5× presentation scale, cassette transport, guided Sunrise setup, dynamic hardware rows, cartridge-slot LEDs, and model editing |
| `diagnostics/test_kbd.c` | Exhaustive international matrix, rollover, alias, PPI, and guest-shortcut checks |
| `diagnostics/test_paste.c` | Paste timing, international punctuation, line-ending, replacement, and cancellation checks |
| `diagnostics/test_psg.c` | PSG registers, generators, envelope shapes, mixer, DAC, and mute checks |
| `diagnostics/test_rtc.c` | RP-5C01 banks, masks, 12/24-hour and test modes, calendar boundaries, offline continuity, corruption rejection, and safe persistence |
| `diagnostics/test_sdcard.c` | SPI initialization, identification, capacity, read/write, multiple transfers, addressing, image safety, and error handling |
| `diagnostics/test_sd_mapper.c` | Firmware banking, expanded subslots, card selection/status, timer, mapper ports, reset, and image lifetime |
| `diagnostics/test_megaflash.c` | Expanded layout, mapper families, flash program/erase/persistence, dual SD, sound, corruption, and flush safety |
| `diagnostics/test_unapinet.c` | openMSXnet handshake and wire results plus real loopback DNS, TCP, UDP, reset, and activity checks |
| `diagnostics/test_scc.c` | SCC compatible/plus maps, waveform sharing, generators, deformation, and audio |
| `diagnostics/test_vdp.c` | Pattern/sprite-mode-1/2 rendering, V9938 bitmap layouts, commands, preloaded transfers, and beam/status checks |

Frontend modules may inspect summarized machine state for presentation, but
guest hardware should not depend on SDL. Keeping that direction of dependency
allows the machine core to run in tests, headless tools, and future non-SDL
hosts.

## Logical display

The window has a 640x520 logical size:

- 640x480 guest framebuffer;
- 16-pixel shared shortcut footer;
- 24-pixel MSX LED strip.

Without firmware, the framebuffer remains a diagnostic scaffold. Once an
MSX1 BIOS is loaded, the TMS9918/TMS9929 core supplies a 256x192 framebuffer
and V9938 modes supply either 256- or 512-dot output with 192 or 212 lines.
The SDL layer preserves that active-image boundary, composes it into a
border-inclusive 640x480 raster representing 280x240 visible VDP dots, and
then presents the finished canvas. This matches openMSX's default horizontal
aperture: a 512-dot active image occupies 586 central canvas pixels rather
than being stretched edge to edge. The active image is vertically centred,
and V9938 R#18 moves it within the raster.

The options overlay is composed after the guest canvas at the fixed 1.5× SDL
debug-font scale used by 1984. It therefore remains crisp and keeps the same
text and row size as the window grows; only windows too small for the complete
640×480 layout use a reduced fit scale. The footer remains a separate
native-pixel presentation layer.

## Hardware profiles and machine catalogue

Compiled hardware profiles define emulation capabilities. Catalogue entries
define the user-visible model names and firmware mappings. The initial
hardware layouts are:

| Setting | MSX1 | MSX2 | Philips NMS 8250 |
|---------|------|------|------------------|
| CPU clock | 3,579,545 Hz | 3,579,545 Hz | 3,579,545 Hz |
| Default RAM | 64 KB | 128 KB | 128 KB |
| VRAM | 16 KB | 128 KB | 128 KB |
| VDP | TMS9918A (NTSC) or TMS9929A (PAL) | V9938 | V9938 |
| Expanded slots | No | Yes | Yes |
| RAM mapper | With RAM above 64 KB | Yes | Yes |
| RTC | No | Yes | Yes |
| Required firmware | BIOS | BIOS + Sub-ROM | BIOS + Sub-ROM; disk ROM optional |

The MSX1 executable layout follows the C-BIOS machine definition: slot 0
contains a 32 KB main ROM and optional 16 KB logo ROM, primary slots 1 and 2
contain independent external cartridge devices, and slot 3 contains RAM.
Each cartridge can be linear, ASCII8, ASCII16, Konami, or Konami SCC.

`1983-models.conf` provides the data-driven layer above these hardware
profiles. A `[model id]` entry supplies `name`, `hardware`, `bios`, `logo`,
`subrom`, `disk_rom`, `floppy_controller`, `floppy_primary_slot`, and
`floppy_secondary_slot`. Paths are resolved relative to that file. A user
can add any number of named models which reuse an implemented hardware layout
without recompiling 1983. The parser caps the catalogue at 64 valid entries,
ignores unknown hardware layouts and duplicate IDs, and falls back to four
built-in entries, including the ready-to-run C-BIOS machine, when no valid
file is available.

**Advanced > Machine model editor**, behind the existing Tinker gate, writes
the same format through `model_catalog_save()`. It edits a copy, validates
the selected definition, atomically replaces the per-user catalogue, reloads
that file, and only then replaces the live catalogue. Editing the currently
selected definition also reloads its firmware and hardware into the running
machine immediately; a floppy-controller topology change is followed by a
full guest reset after the new mapping is installed. Repository and installed
catalogues therefore act as seed data and remain unchanged.

The first concrete MSX2 layout matches the Philips NMS 8250 used by
`../geobench/tools/run_msx.sh`: BIOS/BASIC in primary slot 0, external
primary slots 1 and 2, and expanded primary slot 3 containing the MSX2
sub-ROM in secondary slot 0, the 128 KB internal mapper in slot 2, and the
built-in disk ROM in slot 3/page 1. The expanded-slot register and mapper
ports, V9938 CPU interface, bitmap renderer, command engine, and RTC are
implemented. Its catalogue entry maps the Philips memory-mapped WD2793 and
disk ROM into subslot 3; the controller and one or two raw-image-backed
drives are no longer inferred from the NMS hardware identifier. Generic MSX2
entries can use the same internal mapping, while a generic MSX1 entry may put
the controller in cartridge slot 1 or 2. The external Sunrise
IDE/Nextor cartridge is implemented independently, and the current GeoBench
checkpoint boots on the internal 128 KiB mapper. The SD Mapper V2 cartridge
adds a separate 512 KiB mapper as one half of its real composite hardware,
rather than folding it into a fictitious 640 KiB internal allocation.
MegaFlashROM SCC+ SD supplies another independent 512 KiB mapper inside its
own four-subslot composite cartridge.
Firmware discovery builds on the explicit
`1983-models.conf` paths and the pinned hashes documented in
`BOOT_TARGETS.md`. Only the redistributable C-BIOS MSX1 main and logo ROMs
belong in the repository; all proprietary machine firmware remains local.

The frontend RAM control currently scales the active system mapper from its
profile default through 4 MiB; allocations above 128 KiB live on the heap.
The generic MSX1 layout exposes mapper ports when more than 64 KiB is
selected. SD Mapper V2 and MegaFlashROM RAM remain distinct devices and
combine their mapper-port output with the internal mapper using the bus's
wired-AND semantics.

`config_cartridge_slot_owner()` is the shared authority for physical
cartridge-port reservations. Sunrise IDE, SD Mapper V2, MegaFlashROM SCC+ SD,
SCC, and MSX-MUSIC
reserve slot 1 then slot 2 in deterministic order, matching openMSX and
keeping the physical slot recorded by direct-storage guests stable. Startup
and overlay media operations must consult that function rather than
duplicating extension policy.
`configure_leds()` maps the resolved owner and cartridge presence onto the
two physical slot indicators. Sunrise storage activity uses the dedicated
IDE LED, while both SD cartridges use the dedicated SD A/B LEDs. The
port-mapped TCP/IP UNAPI bridge owns no cartridge slot and reports traffic
through the dedicated network LED.

## TCP/IP UNAPI boundary

`src/unapinet.c` is the host half of openMSXnet's v1 bridge. It consumes only
I/O ports `0x28`/`0x29`; `src/msx.c` exposes those through a generic optional
I/O-device callback rather than pretending the bridge is a cartridge. The
guest half remains the upstream v0.9.7 `UNAPINET.COM` TSR, whose detection
magic, command numbers, mixed-endian wire records, four-handle limits, and
error quirks are compatibility requirements.

Socket and resolver work must never block the Z80 frame loop. TCP connects
and all descriptors are non-blocking, data is held in fixed-size buffers,
and DNS resolution runs on one SDL thread with generation-based cancellation.
The frame loop polls ready sockets; reset, toggle-off, and process shutdown
invalidate DNS results and close every descriptor. Tests use real host
loopback sockets and the exact v1 command stream without requiring external
Internet access or the third-party TSR binary.

## Sunrise and ATA boundaries

`src/sunrise.c` owns the cartridge-specific bus contract. It decodes the
bank/control writes and the memory-mapped IDE windows, then converts the
Sunrise low/high-byte latch into 16-bit ATA data transfers. `src/ata.c` knows
nothing about MSX slots or SDL and owns the task-file state and host image.
This split makes the ATA backend reusable by future controllers.

Raw images default to `rb`. Explicit read/write mode uses `r+b`, buffers a
guest sector until all 512 bytes arrive, and tracks completed dirty sectors
until ATA FLUSH CACHE, replacement, ejection, or shutdown. Incomplete
transfers are discarded safely by reset. Flush failures keep the image
mounted and visible to the frontend instead of silently losing buffered
data.

The optional full-system checkpoint is enabled with:

```sh
MSX_NMS8250_DIR=ROMS \
MSX_NEXTOR_SUNRISE_ROM=/path/to/Nextor-2.1.1.SunriseIDE.ROM \
MSX_NEXTOR_IDE_IMAGE=/path/to/GBMSX.IMG \
make check
```

It omits the NMS disk ROM, pins the test RTC to 1983-01-01, boots for 2,001
PAL frames, and verifies the CPU, slot registers, mapper registers, VDP state,
VRAM population, and deterministic GeoBench-desktop framebuffer hash.

## SD Mapper V2 boundaries

`src/sd_mapper.c` owns the composite cartridge contract: expanded-slot
selection, primary and alternate driver ROM halves, firmware banking,
SPI/status/timer registers, dual card-select lines, and the cartridge's
independent mapper ports. `src/sdcard.c` knows nothing about MSX slots and
owns one SPI protocol endpoint and host card image. The two `SdCard`
instances therefore remain separate even when firmware selects both.

The external mapper must not be merged into the machine's internal RAM
allocation. If both devices answer ports `0xFC` through `0xFF`, `src/msx.c`
combines their read values with bitwise AND, matching the open-collector MSX
bus convention. The cartridge reset value is `3,2,1,0`; configuration
switches persist independently from guest-controlled segment registers.

Card writes are committed only after a full 512-byte data block and its two
CRC bytes have arrived. Reset drops an incomplete protocol transfer but keeps
mounted media and completed dirty sectors. Replacement and ejection flush and
synchronize dirty data; a host failure keeps the card mounted and surfaces
the error. Both sockets share an explicit read-only/read-write policy and
default to read-only.

The optional full-system checkpoint is enabled with:

```sh
MSX_SD_MAPPER_BIOS_ROM=/path/to/MSX.ROM \
MSX_SD_MAPPER_ROM=/path/to/SDXC110.ROM \
MSX_SD_MAPPER_IMAGE=/path/to/card.img \
make check
```

It boots the generic PAL MSX1 for 900 frames, requires real card activity,
and verifies that firmware, the expanded cartridge, its 512 KiB mapper, and
Nextor-loaded video state all remain live. Exact CPU and framebuffer values
are reported but intentionally not pinned because users may supply different
valid BIOS and card contents.

## MegaFlashROM SCC+ SD boundaries

`src/megaflash.c` owns the complete cartridge bus contract instead of
presenting its parts as independent extensions. It decodes the four internal
subslots, all MegaFlash and MegaSD mapper registers, the 512 KiB mapper, SCC-I
windows, cartridge PSG ports, and M29W640GB commands. Its two `SdCard`
instances reuse the storage protocol and conservative host-image lifetime
from `src/sdcard.c`.

The user-supplied initial image of up to 8 MiB is immutable seed material.
Short images are padded with erased bytes. Writable guest flash lives in
`flash/megaflashrom-scc-plus-sd.flash` beside the active
configuration. A state file must be exactly 8 MiB; an invalid existing file
is an error rather than a reason to silently overwrite it from the seed.
Saves use a synchronized temporary and atomic replacement. Dirty flash or SD
media prevents unsafe disconnect if its host flush fails.

The optional real-device checkpoint is enabled with:

```sh
MSX_MEGAFLASH_BIOS_ROM=/path/to/MSX.ROM \
MSX_MEGAFLASH_ROM=/path/to/mfrsd.rom \
make check
```

It boots a PAL MSX1 for 1,200 frames and requires cartridge connection,
the `NEXTOR.SYS` banner in VRAM, sustained CPU execution, and populated video
state. `MSX_MEGAFLASH_IMAGE` may additionally mount a card for the run; the
preflash boots its internal ROM disk first, so MegaSD traffic is verified
independently at the component boundary. The flash image, programmed contents,
and optional card remain local test inputs.

## WD2793 and floppy boundaries

`src/wd2793.c` owns controller registers, commands, side/drive/motor
selection, rotational index pulses, active-low IRQ/DRQ reporting, and
complete-sector transfer buffers. `src/floppy.c` knows nothing about MSX
slots or controller commands;
it validates conventional raw MSX DSK geometry and owns host file I/O. The MSX
bus exposes the Philips registers only when the catalogue-configured
slot/subslot is selected. Mapping the device into primary slot 1 or 2 also
reserves that physical cartridge port. This keeps machine-specific controller
wiring separate from the reusable image backend and provides the boundary for
external disk-interface cartridges. CDX-2 is the first such cartridge: its
user-supplied 16 KiB ROM (or the jumper-selected half of a 32 KiB 27C256
image) and D0h-D4h port gate share one lifecycle and reserve one physical
cartridge slot. RDF600 follows the same lifecycle but uses a
separate TC8566AF command core and the TDC-600 memory decode; both controllers
share only the raw-image and safe host-I/O layer.

Both images start read-only unless read/write is explicitly selected.
Completed writes become dirty in the image backend, while reset can discard
an incomplete controller transfer without changing the host file. Image
replacement and ejection flush and synchronize dirty data. Failure preserves
the attached image and error state. Extensions owns the optional second-drive
control; Advanced owns the access mode, while Media owns insertion and safe
ejection.

## RTC and CMOS boundaries

`src/rtc.c` owns the RP-5C01 register model and the portable CMOS byte
format. It has no SDL dependency and accepts explicit host timestamps at
initialization, load, and save boundaries. The machine scheduler supplies
emulated Z80 cycles; it never polls wall time while guest code is running.
This keeps accelerated and headless runs reproducible while still allowing
the battery clock to catch up between processes.

`src/msx.c` owns the active persistence path and dirty/error status.
`src/config.c` derives a separate sanitized filename from the selected
machine ID beneath the active configuration directory. `src/main.c` attaches
that file at startup and flushes it at shutdown. The overlay detaches and
flushes before replacing a machine, then attaches the new model's clock.
Load failure is non-destructive: a host-seeded clock remains live and the
same requested path stays attached so a future atomic save can repair it.
A save failure keeps the old attachment and blocks the transition that
would otherwise discard dirty CMOS.

The on-disk header and checksum are deliberately independent of the in-memory
structure layout. Do not serialize `MsxRtc` directly: padding, control state,
and cycle accumulators are not battery-backed data. Add format migrations by
version rather than weakening exact-size, checksum, BCD, flag, or calendar
validation. Tests use `rtc_init_at()` and explicit load/save timestamps;
frontend smoke tests may use `--config /dev/null` to disable persistence.

## Bus and port assumptions

The initial implementation work should preserve these standard MSX
relationships:

- The address space is four 16 KB pages. PPI port A at `0xA8` selects one of
  four primary slots independently for each page.
- PPI port B at `0xA9` reads the active-low keyboard row selected by the low
  nibble of port C at `0xAA`; `0xAB` provides bit set/reset control. Port C
  bit 4 is the active-low cassette motor and bit 5 is cassette output.
- The TMS9918-family and V9938 use the standard `0x98`/`0x99` data and control
  ports; the V9938 additionally exposes its MSX2 command and palette paths.
- The AY-compatible PSG uses the standard `0xA0` through `0xA2` register,
  write, and read ports.
- Expanded primary slots have four secondary slots. Their page selection
  register is visible through address `0xFFFF` when page 3 selects that
  expanded primary slot; reads use the MSX inverted representation.
- The MSX2 memory mapper exposes one RAM segment register per 16 KB page.
- The MSX2 RTC is selected and accessed through ports `0xB4` and `0xB5`.

These notes were cross-checked against the openMSX 21.0 machine definitions
and CPU/input implementations. The primary-slot, complete international
keyboard matrix, PPI register, VDP-port, and PSG-register surfaces above are
now present. The NMS 8250 secondary slots, internal mapper registers, MSX2
RTC, V9938 bitmap modes, and beam-timed command handshakes are also present;
V9938 VR/HR status follows the emulated beam and the VDP IRQ is level
sensitive. V9938 sprite mode 2 is rendered in SCREEN 4 through SCREEN 8.
Completed V9938 display rows are committed progressively before timed VRAM
mutations and relevant register or palette writes. Alternate national
keyboard matrices and within-scanline pixel timing are not implemented.

## Keyboard input

The machine core owns eleven active-low row bytes and per-position reference
counts. This keeps the hardware path independent of SDL and lets two physical
host keys that share a matrix position—most notably left and right Shift—be
released independently. The SDL adapter uses physical scancodes and the
standard international matrix transcribed from openMSX 21.0.

Unmodified function keys belong to the sibling frontend. Shift+F1 through
Shift+F5 reach the MSX function keys, while Shift+F7 and Shift+F8 produce
SELECT and STOP. The adapter temporarily removes host Shift from the matrix
for those chords. Focus loss, overlay entry, and machine reset clear both host
tracking and the guest matrix to prevent stuck keys. The generic machine
currently provides idealized simultaneous-key rollover rather than
model-specific electrical ghosting.

Ctrl+V obtains UTF-8 clipboard text from SDL, releases the physical shortcut,
and hands a private copy to `src/paste.c`. Printable ASCII, Tab, Backspace,
and newline are translated using the international openMSX Unicode map;
carriage returns and unsupported UTF-8 bytes are skipped. Paste does not add
a trailing Return. A queued character is held for two complete frames with a
one-frame gap, after an initial three-frame shortcut-release delay. Pausing
also pauses the queue; reset, overlay entry, or focus loss cancels it and
releases any synthetic key. OS key auto-repeat never restarts the queue, and
the keyboard adapter ignores repeated key-down events so a chord held while
the queue is typing cannot leak back into the guest matrix.

## TMS9918-family sprites

Sprite mode 1 evaluates the 32-entry attribute table in index order for every
visible scanline. It implements the four-sprite limit, lower-index priority,
the fifth-sprite number, collision latching, transparent color zero, early
clock, 8x8/16x16 patterns, magnification, the one-line Y offset, 8-bit vertical
wrap, and the `0xD0` list terminator. Pattern dots with color zero remain
collision-active on the MSX1 VDP even though they do not draw.

The TMS9918-family path evaluates this state at the frame boundary. Its
cycle-level changes to sprite attributes, patterns, display enable, or VDP
registers during an active frame are not timed yet.

## V9938 sprite mode 2

SCREEN 4 through SCREEN 8 use the V9938's 32-entry mode-2 sprite engine. The
renderer evaluates up to eight sprites per visible scanline, reports the ninth
sprite through S#0, and reads each sprite's color and EC/CC/IC attributes from
its 16-byte per-line color table. CC entries combine color codes with the
nearest higher-priority base sprite, while CC and IC entries remain excluded
from collision detection.

The 1 KiB attribute window follows the R#5/R#11 address masks: its first 512
bytes are the color table and its second half contains the Y/X/pattern records.
SCREEN 7 and SCREEN 8 rotate those logical addresses through the two 64 KiB
VRAM planes. SCREEN 6 expands one logical sprite dot to two independently
colored pixels; SCREEN 7 duplicates it; SCREEN 8 uses the V9938's fixed sprite
colors.

Transparent color zero, R#8 TP/SPD, 8x8/16x16 size, magnification, early clock,
vertical scrolling, the `0xD8` terminator, and 192/212-line display heights are
implemented. Collision coordinates latch in S#3-S#6 with their hardware
offsets and reset when S#5 is read. On the V9938, sprites are evaluated with
the VRAM and register state visible when each completed scanline is committed.
Individual sprite fetch positions within that scanline are not timed yet.

The functional model is cross-checked against the
[Yamaha V9938 Technical Data Book](https://map.grauw.nl/resources/video/yamaha_v9938.pdf)
and the
[V9938 Programmer's Guide](https://ia800409.us.archive.org/25/items/tms9918_guide/V9938-programmers-guide%20insecure.pdf).
The later timing pass should follow openMSX's measured
[V9938 VRAM timings](https://openmsx.org/vdp-vram-timing/vdp-timing.html)
and [part II](https://openmsx.org/vdp-vram-timing/vdp-timing-2.html).

## V9938 scanline interrupts

The beam scheduler implements the V9938 horizontal interrupt path. R#19 is
compared with the display-line counter after applying the R#23 vertical
offset. A match occurs at the beginning of that line's right border, including
R#18 horizontal/vertical adjustment, 192/212-line mode, PAL/NTSC frame
geometry, and the counter's limited carry into the following frame.

With R#0 IE1 enabled, a match latches FH in S#1 and asserts the VDP interrupt
until S#1 is read or IE1 is disabled. With IE1 disabled, FH remains observable
as the hardware's short beam-position pulse but does not assert IRQ. Vertical
F/IE0 and horizontal FH/IE1 are independent sources on the shared IRQ output:
reading S#0 acknowledges only vertical blank, while reading S#1 acknowledges
only the horizontal match.

The match position is converted from the V9938's 1368 ticks per scanline to
the current CPU-frame budget, so normal PAL/NTSC execution does not assume
one CPU cycle per VDP tick. The renderer uses the same beam position to commit
completed rows; horizontal changes within one row remain later timing work.

## V9938 VRAM and command timing

CPU reads and writes through V9938 port `0x98` reserve the first free VRAM
access at least 16 VDP ticks after the request. Read-ahead loads and address
increments happen when that access executes, while writes update the shared
CPU data latch immediately. A second request arriving before the first access
replaces the pending read/write operation without moving its reserved time,
matching the V9938's behaviour under excessively fast traffic.

The scheduler uses measured screen-off, bitmap-with-sprites,
bitmap-without-sprites, character, and text access tables. TMS9918 accesses
remain immediate, as do V9938 accesses in standalone functional tests without
an initialized beam clock. R#14 carry and planar SCREEN 6/7 remapping are
applied at execution time. If a CPU request and command operation reach the
same slot, the CPU access wins and the command operation moves to the next
free slot without clearing CE.

The complete V9938 bitmap-command set now runs progressively from an
independent command clock tied to the emulated beam. Autonomous operations
leave S#2 CE asserted while individual pixels or packed bytes are processed.
Their operation-dependent spacings account for POINT/PSET reads and writes,
SRCH/LINE steps, logical pixel read-modify-write work, high-speed byte
transfers, and row overhead.

Each autonomous step is aligned to a measured free bitmap-mode VRAM access
position within the V9938's 1368-tick scanline. Separate schedules cover
screen-off, sprites-disabled, and sprites-enabled operation. VRAM and command
result registers therefore change as their accesses complete instead of being
calculated in one batch when R#46 is written.

LMMC and HMMC clear TR after accepting R#44, then raise it again when the
next transfer interval expires. A write made while TR is low remains pending
and is consumed at that ready event, preserving the Sub-ROM's preloaded-color
ordering. LMCM similarly clears TR after an S#7 read and exposes the next
pixel only after its VRAM-read interval. CE remains active through the final
transfer interval. Starting another R#46 command cancels the old scheduled
completion.

Command time is stored in V9938 ticks and advanced with fixed-point conversion
from the current PAL/NTSC CPU-frame budget, including commands which span a
frame boundary. Logical read-modify-write operations currently remain one
atomic command step. CPU and command VRAM writes flush display rows whose
right border the beam has reached, so later rows observe the new VRAM without
redrawing committed output. Register and palette writes use the same
boundary. Pixel-level changes within the active row remain later timing work.
The access schedules and operation spacings follow
openMSX's measured
[V9938 VRAM timings](https://openmsx.org/vdp-vram-timing/vdp-timing.html)
and [part II](https://openmsx.org/vdp-vram-timing/vdp-timing-2.html).

## PSG audio

The machine core contains a host-independent AY-3-8910/YM2149 PSG. Generic
MSX1 uses the AY variant and Generic MSX2 uses the YM variant. The core models
the three square-wave channels, the 17-bit noise generator, all sixteen
envelope shapes, fixed and envelope volume curves, mixer gating, and
volume-register DAC output. Register readback preserves the documented
AY-versus-YM differences.

The PSG runs at 1,789,773 Hz. The Z80 bus tick callback advances machine time
within each instruction, so writes through ports `0xA0` through `0xA2` affect
the generated signal at their I/O-cycle position rather than at the next
frame boundary. Samples are accumulated deterministically at 44.1 kHz,
lightly DC-blocked and filtered, then passed as signed 16-bit mono audio to an
SDL3 stream. Headless and unthrottled execution still advances the PSG and is
covered by component tests, but deliberately does not open a host audio
device.

PSG register 14 reports the selected connector's six active-low input lines,
the international keyboard-layout signal, and the cassette comparator.
Register 15 selects Joy Port A or B and supplies their separate pin-8
outputs. A configured mouse advances through signed X/Y nibbles on pin-8
strobes, including the alternate zero cycle and 1.5 ms resynchronization
modeled by openMSX. Port B also drives the active-low Kana LED. The core owns
both joystick latches and both SDL-independent mouse protocol states.

The SDL3 adapter opens the first available gamepad, follows add/remove
events, and polls its D-pad, left stick, and south/east face buttons once per
frame. The frontend clears both latches before routing that state through the
live Main Input setting, but only when the chosen connector is configured as
Joystick. When it is Mouse, relative motion and two buttons feed that
connector after a click captures the pointer. Ctrl+Enter follows the sibling
emulators' release gesture; F9, reset, and focus loss also release and clear
host input.

`src/cassette.c` converts standard CAS byte streams to a deterministic
14,976 Hz signed waveform. It follows the BIOS-visible 3,744 baud framing
and leader conventions used by openMSX, but has no dependency on SDL or host
wall time. PPI motor/output changes and PSG comparator reads synchronize the
transport to the current machine cycle. Tests use generated CAS fixtures so
no copyrighted tape software is required.

## Verification

Run the profile tests with:

```sh
make check
```

Run a short frontend smoke test without a desktop:

```sh
./1983 --headless --unthrottled --exit-after 10 \
  --config /tmp/1983-smoke.conf
```

Script a paste into the guest for headless verification. `--paste-at` waits
for boot, `--paste-repeat` re-invokes the trigger like an OS auto-repeat
storm, `--screenshot` captures the final frame, and `--dump-ram` prints guest
memory on exit for inspecting what the guest actually received:

```sh
./1983 --headless --unthrottled --exit-after 1200 \
  --cart /path/to/rom --paste-at 300 --paste-repeat 30 \
  --paste-text $'10 PRINT "HI"\nLIST\n' --screenshot /tmp/paste.ppm \
  --dump-ram 0x8000:64
```

Run the pinned C-BIOS checkpoint against the bundled C-BIOS 0.29 files:

```sh
MSX_CBIOS_DIR=ROMS make check
```

Run the Philips NMS 8250 firmware checkpoint with:

```sh
MSX_NMS8250_DIR=/path/to/nms8250-roms make check
```

Add a local conventional floppy image to exercise the real disk-ROM boot
path:

```sh
MSX_NMS8250_DIR=/path/to/nms8250-roms \
MSX_NMS8250_DSK=/path/to/software.dsk \
make check
```

The C-BIOS test runs 180 NTSC frames to a stable no-cartridge state, then
resets and verifies that C-BIOS discovers and launches a synthetic linear ROM
cartridge. A separate CPU checkpoint executes from an ASCII8 window, switches
a bank through the MSX bus, and copies a banked sentinel into RAM. The NMS
8250 test verifies firmware loading, expanded slot discovery, mapper setup,
and SCREEN 6 startup after 200 PAL frames. With `MSX_NMS8250_DSK`, it
additionally boots for 3,000 frames through the internal disk ROM, verifies
WD2793 activity and geometry, and checks that guest execution and video
continue after disk access.
When the MSX Diagnostics cartridge is available, its MSX2 startup and menu
handoff can be included with:

```sh
MSX_NMS8250_DIR=/path/to/nms8250-roms \
MSX_DIAG_ROM=/path/to/diag.rom make check
```

Keep deterministic tests below SDL first; reserve rendered-frame and
end-to-end tests for interactions that cannot be proved reliably at the
component boundary.
