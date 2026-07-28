# 1983 development notes

This document records the boundaries and hardware assumptions established by
the initial scaffold. It is intentionally narrower than a complete MSX
hardware specification.

## Source layout

| Files | Responsibility |
|-------|----------------|
| `src/main.c` | Process lifetime, command line, SDL event loop, and shared function-key bindings |
| `src/config.*` | Defaults, normalization, persistent settings, and platform-specific configuration path |
| `src/display.*` | SDL window and renderer, fixed logical canvas, framebuffer presentation, footer, and screenshots |
| `src/overlay.*` | F9 options workflow and live application of frontend and machine-profile settings |
| `src/leds.*` | Shared bottom status strip and MSX-specific indicator definitions |
| `src/notify.*` | On-screen and console notifications |
| `src/ui.*` | Small renderer primitives used by the frontend |
| `src/msx.*` | Emulator-facing machine profiles and the future memory/I/O boundary |
| `tests/test_msx.c` | Profile, RAM normalization, reset, pause, and frame-state checks |

Frontend modules may inspect summarized machine state for presentation, but
guest hardware should not depend on SDL. Keeping that direction of dependency
allows the machine core to run in tests, headless tools, and future non-SDL
hosts.

## Logical display

The window has a 640x520 logical size:

- 640x480 guest framebuffer;
- 18-pixel shared shortcut footer;
- 22-pixel MSX LED strip.

The initial framebuffer is a diagnostic scaffold, not a VDP implementation.
The TMS9918/TMS9929 and V9938 should eventually produce their own frame data
through the machine boundary without owning the host window.

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

The next machine layer should make vendor and firmware layouts data-driven
rather than accumulating model checks throughout device code.

## Bus and port assumptions

The initial implementation work should preserve these standard MSX
relationships:

- The address space is four 16 KB pages. PPI port A at `0xA8` selects one of
  four primary slots independently for each page.
- Ports `0xA9` through `0xAB` provide the keyboard/cassette PPI paths and PPI
  control.
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
and CPU-interface implementation. Device behaviour and timing still need
dedicated documentation and tests before implementation.

## Near-term implementation order

1. Bring in the sibling Z80 core behind memory-read, memory-write, input, and
   output callbacks.
2. Add a slot bus with explicit primary/secondary slot ownership and ROM/RAM
   devices.
3. Implement the PPI sufficiently for slot selection and the keyboard matrix.
4. Load a deliberately selected, legally usable MSX1 firmware set.
5. Implement enough TMS9918/TMS9929 video and interrupt behaviour to reach a
   repeatable firmware boot checkpoint.
6. Add PSG output only after the CPU, slot, PPI, and VDP boundaries are covered
   by focused tests.

The firmware decision is to support both C-BIOS and user-supplied BIOS/BASIC
sets with clearly different capabilities. C-BIOS is the redistributable
cartridge-oriented default; supplied firmware is the full BASIC and peripheral
compatibility path.

The first mass-storage boot milestone is Nextor 2.1.x on a generic MSX2 with a
128 KB memory mapper and Sunrise ATA-IDE cartridge. The Sunrise wrapper should
be implemented independently from the ATA task-file backend. The compact ATA
backend in the 1984 sibling can be adapted and tested in isolation, while the
Sunrise memory map and bank-control behaviour are cross-checked against
openMSX. See [`BOOT_TARGETS.md`](BOOT_TARGETS.md) for the boot matrix and
licensing boundaries.

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

As devices arrive, add deterministic tests below the SDL layer first. Reserve
rendered-frame and end-to-end boot tests for interactions that cannot be
proved reliably at the component boundary.
