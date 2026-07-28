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
| `src/z80.*` | Sibling Z80 core and host-independent bus callback contract |
| `src/vdp.*` | TMS9918/TMS9929 ports, state, interrupts, pattern modes, and sprite-mode-1 renderer |
| `tests/test_msx.c` | Profiles, slots, CPU execution, VDP ports/rendering, and optional C-BIOS boot checks |
| `tests/test_kbd.c` | Exhaustive international matrix, rollover, alias, PPI, and guest-shortcut checks |
| `tests/test_psg.c` | PSG registers, generators, envelope shapes, mixer, DAC, and mute checks |
| `tests/test_vdp.c` | Sprite size, magnification, priority, clipping, scanline limit, collision, and status-latch checks |

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
which the SDL layer scales into the 640x480 guest canvas. The V9938 remains a
profile boundary rather than an implemented MSX2 display.

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
ports are implemented; the V9938, WD2793 controller, and RTC are not. The
GeoBench configuration will then add independent SunriseIDE/Nextor and
512 KB memory-mapper extensions. These are two mapper devices, not one
combined RAM allocation. Firmware discovery will reuse the recursive
`~/.openMSX/share/systemroms` pool and match the pinned hashes documented in
`BOOT_TARGETS.md`; no machine ROM belongs in the repository.

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
now present. The NMS 8250 secondary slots and internal mapper registers are
also present; alternate national keyboard matrices, the RTC, joystick/mouse
PSG inputs, and accurate VDP timing are not.

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
slot discovery, and mapper setup after 200 PAL frames. Keep deterministic
tests below SDL first; reserve rendered-frame and end-to-end tests for
interactions that cannot be proved reliably at the component boundary.
