# 1983 development notes

This document records the boundaries and hardware assumptions established by
the initial scaffold. It is intentionally narrower than a complete MSX
hardware specification.

## Source layout

| Files | Responsibility |
|-------|----------------|
| `src/main.c` | Process lifetime, command line, SDL event loop, and shared function-key bindings |
| `src/audio.*` | SDL3 audio-stream lifetime and host sample submission |
| `src/config.*` | Defaults, normalization, persistent settings, and platform-specific configuration path |
| `src/display.*` | SDL window and renderer, fixed logical canvas, framebuffer presentation, footer, and screenshots |
| `src/kbd.*` | SDL scancode translation and shared frontend/guest function-key routing |
| `src/overlay.*` | F9 options workflow and live application of frontend and machine-profile settings |
| `src/leds.*` | Shared bottom status strip and MSX-specific indicator definitions |
| `src/notify.*` | On-screen and console notifications |
| `src/ui.*` | Small renderer primitives used by the frontend |
| `src/msx.*` | Machine profiles, slot-aware memory and I/O bus, active-low keyboard matrix, ROM loading, and frame scheduler |
| `src/psg.*` | Host-independent AY-3-8910/YM2149 tone, noise, envelope, mixer, and sample generation |
| `src/rtc.*` | Host-independent RP-5C01 register banks, CMOS, control ports, and emulated-time calendar |
| `src/z80.*` | Sibling Z80 core and host-independent bus callback contract |
| `src/vdp.*` | TMS9918/TMS9929 renderer plus the V9938 register, palette, beam status, 128 KB VRAM, bitmap, sprite-mode-2, and command engine |
| `tests/test_msx.c` | Profiles, slots, CPU execution, device ports, interrupt acknowledgement, and optional C-BIOS/NMS 8250/diagnostic boot checks |
| `tests/test_kbd.c` | Exhaustive international matrix, rollover, alias, PPI, and guest-shortcut checks |
| `tests/test_psg.c` | PSG registers, generators, envelope shapes, mixer, DAC, and mute checks |
| `tests/test_rtc.c` | RP-5C01 banks, masks, reset behavior, calendar rollover, and clock advancement |
| `tests/test_vdp.c` | Pattern/sprite-mode-1/2 rendering, V9938 bitmap layouts, commands, preloaded transfers, and beam/status checks |

Frontend modules may inspect summarized machine state for presentation, but
guest hardware should not depend on SDL. Keeping that direction of dependency
allows the machine core to run in tests, headless tools, and future non-SDL
hosts.

## Logical display

The window has a 640x520 logical size:

- 640x480 guest framebuffer;
- 18-pixel shared shortcut footer;
- 22-pixel MSX LED strip.

Without firmware, the framebuffer remains a diagnostic scaffold. Once an
MSX1 BIOS is loaded, the TMS9918/TMS9929 core supplies a 256x192 framebuffer
which the SDL layer scales into the 640x480 guest canvas. V9938 bitmap modes
supply either 256- or 512-dot output and 192 or 212 lines through the same
dynamic framebuffer boundary.

## Initial generic profiles

The profiles are compatibility starting points rather than claims about every
vendor machine:

| Setting | Generic MSX1 | Generic MSX2 |
|---------|--------------|--------------|
| CPU clock | 3,579,545 Hz | 3,579,545 Hz |
| Default RAM | 64 KB | 128 KB |
| VRAM | 16 KB | 128 KB |
| VDP | TMS9918A (NTSC) or TMS9929A (PAL) | V9938 |
| Expanded slots | No | Yes |
| RAM mapper | No | Yes |
| RTC | No | Yes |

The MSX1 executable layout follows the C-BIOS machine definition: slot 0
contains a 32 KB main ROM and optional 16 KB logo ROM, slot 1 contains one
external plain cartridge, slot 2 is open, and slot 3 contains RAM. Vendor and
firmware layouts should eventually become data-driven rather than
accumulating model checks throughout device code.

The first concrete MSX2 layout matches the Philips NMS 8250 used by
`../geobench/tools/run_msx.sh`: BIOS/BASIC in primary slot 0, external
primary slots 1 and 2, and expanded primary slot 3 containing the MSX2
sub-ROM in secondary slot 0, the 128 KB internal mapper in slot 2, and the
built-in disk ROM in slot 3/page 1. The expanded-slot register and mapper
ports, V9938 CPU interface, bitmap renderer, command engine, and RTC are
implemented; the WD2793 controller is not. The GeoBench
configuration will then add
independent SunriseIDE/Nextor and 512 KB memory-mapper extensions. These are
two mapper devices, not one combined RAM allocation. Firmware discovery will
reuse the recursive `~/.openMSX/share/systemroms` pool and match the pinned
hashes documented in `BOOT_TARGETS.md`; no machine ROM belongs in the
repository.

## Bus and port assumptions

The initial implementation work should preserve these standard MSX
relationships:

- The address space is four 16 KB pages. PPI port A at `0xA8` selects one of
  four primary slots independently for each page.
- PPI port B at `0xA9` reads the active-low keyboard row selected by the low
  nibble of port C at `0xAA`; `0xAB` provides bit set/reset control.
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
Alternate national keyboard matrices, joystick/mouse PSG inputs, and
scanline-accurate rendering of mid-frame changes are not.

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

## TMS9918-family sprites

Sprite mode 1 evaluates the 32-entry attribute table in index order for every
visible scanline. It implements the four-sprite limit, lower-index priority,
the fifth-sprite number, collision latching, transparent color zero, early
clock, 8x8/16x16 patterns, magnification, the one-line Y offset, 8-bit vertical
wrap, and the `0xD0` list terminator. Pattern dots with color zero remain
collision-active on the MSX1 VDP even though they do not draw.

This is scanline-aware evaluation of the VRAM state at the frame boundary.
Cycle-level changes to sprite attributes, patterns, display enable, or VDP
registers during an active scanline are not timed yet.

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
offsets and reset when S#5 is read. Sprite VRAM reads are still evaluated from
the frame-boundary state rather than at their measured per-scanline access
slots.

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
one CPU cycle per VDP tick. This is functional interrupt timing; VRAM access
slot contention and mid-scanline renderer changes remain later timing work.

## V9938 command timing

The complete V9938 bitmap-command set retains its functional renderer and now
runs an independent command clock from the emulated beam. Autonomous
operations leave S#2 CE asserted for an operation-dependent interval derived
from the measured V9938 command spacings: POINT/PSET and SRCH/LINE use their
read/write costs, logical moves account for pixel read-modify-write work, and
high-speed moves account for packed-byte transfers and row overhead.

LMMC and HMMC clear TR after accepting R#44, then raise it again when the
next transfer interval expires. A write made while TR is low remains pending
and is consumed at that ready event, preserving the Sub-ROM's preloaded-color
ordering. LMCM similarly clears TR after an S#7 read and exposes the next
pixel only after its VRAM-read interval. CE remains active through the final
transfer interval. Starting another R#46 command cancels the old scheduled
completion.

Command time is stored in V9938 ticks and advanced with fixed-point conversion
from the current PAL/NTSC CPU-frame budget, including commands which span a
frame boundary. For this first timing pass, the command's VRAM result is still
calculated at issue time; progressive per-access mutation and contention with
display, sprite, and CPU VRAM slots remain future work. The operation spacings
and that later contention pass follow openMSX's measured
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

PSG port A currently reports inactive joystick inputs, the international
keyboard-layout signal, and an empty-cassette comparator. Port B drives the
active-low Kana LED. Joystick, mouse, and real cassette signals remain future
peripheral work.

## Near-term implementation order

The first five implementation steps—sibling Z80 integration, primary slots,
PPI slot selection, external firmware loading, and a repeatable C-BIOS/VDP
checkpoint—are now represented in code and focused tests. The next sequence
is:

1. Add ASCII8/ASCII16, Konami, and Konami SCC cartridge mappers with explicit
   override hooks.
2. Reach a deterministic BASIC prompt with a user-supplied BIOS/BASIC set.
3. Add joystick input, cassette loading, alternate national keyboard layouts,
   and a small redistributable MSX1 compatibility corpus.
4. Refine VDP timing so mid-frame register and VRAM changes take effect on
   the correct scanline.
5. Add deterministic snapshot and audio-capture surfaces for compatibility
   investigations.

The firmware decision is to support both C-BIOS and user-supplied BIOS/BASIC
sets with clearly different capabilities. C-BIOS is the redistributable
cartridge-oriented default; supplied firmware is the full BASIC and peripheral
compatibility path.

The first mass-storage boot milestone matches the GeoBench setup: Nextor
2.1.1 on a PAL Philips NMS 8250 with its internal 128 KB mapper, an external
512 KB mapper, and a Sunrise ATA-IDE cartridge. The Sunrise wrapper should be
implemented independently from the ATA task-file backend. The compact ATA
backend in the 1984 sibling can be adapted and tested in isolation, while the
Sunrise memory map and bank-control behaviour are cross-checked against
openMSX. See [`BOOT_TARGETS.md`](BOOT_TARGETS.md) for the pinned ROM hashes,
boot matrix, and licensing boundaries.

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

Run the pinned C-BIOS checkpoint when C-BIOS 0.29 is available:

```sh
MSX_CBIOS_DIR=/path/to/cbios make check
```

Run the in-progress Philips NMS 8250 firmware checkpoint with:

```sh
MSX_NMS8250_DIR=/path/to/nms8250-roms make check
```

The C-BIOS test runs 180 NTSC frames to a stable no-cartridge state, then
resets and verifies that C-BIOS discovers and launches a synthetic plain ROM
cartridge. The NMS 8250 test currently verifies firmware loading, expanded
slot discovery, mapper setup, and SCREEN 6 startup after 200 PAL frames.
When the MSX Diagnostics cartridge is available, its MSX2 startup and menu
handoff can be included with:

```sh
MSX_NMS8250_DIR=/path/to/nms8250-roms \
MSX_DIAG_ROM=/path/to/diag.rom make check
```

Keep deterministic tests below SDL first; reserve rendered-frame and
end-to-end tests for interactions that cannot be proved reliably at the
component boundary.
