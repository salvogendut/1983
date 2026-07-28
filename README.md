# 1983 - MSX / MSX2 emulator

1983 is a compatibility-focused emulator for MSX and MSX2 computers. Rather
than reproducing one particular manufacturer's machine, it aims to provide a
generic, configurable implementation of the MSX architecture capable of
running most software written for the first two generations of the standard.

It is a sibling project of [1984](https://github.com/salvogendut/1984), the
Amstrad CPC emulator, and [1985](https://github.com/salvogendut/1985), the
Amstrad PCW emulator. The name marks 1983, the year in which the MSX standard
was introduced.

Like its siblings, 1983 is written in C using SDL3. It follows their window,
overlay, shortcut, notification, LED, and configuration conventions so that
the three emulators look and behave as members of the same family. Portable
code and minimal platform-specific dependencies are intended to keep it
available on as many SDL3-supported systems as practical.

> **Project status:** the first executable MSX1 slice is in place. It runs the
> sibling Z80 core against a primary-slot bus, boots C-BIOS, accepts plain ROM
> cartridges, and renders the character/pattern modes needed by the initial
> firmware checkpoint. The standard international MSX keyboard matrix is
> connected to SDL input, and the TMS9918-family sprite engine is implemented.
> Audio output, cartridge mappers, storage, and MSX2 execution are still to
> come.

## Current implementation

- Autotools-based C11 and SDL3 build, following the sibling projects.
- Desktop/AppStream integration with the project artwork in standard hicolor
  sizes and a multi-resolution Windows executable icon.
- Resizable 640x480 guest display with a footer, LED bar, fullscreen and
  integer window scaling.
- F9 options overlay with General, Media, Extensions, and Advanced sections.
- Persistent generic MSX1/MSX2, PAL/NTSC, RAM, display, extension, and
  notification settings.
- Power, Caps, Kana, drive, and cassette LEDs with hover descriptions.
- Cycle-budgeted Z80 execution and maskable interrupts, adapted from the
  sibling projects behind machine-owned memory and I/O callbacks.
- Four 16 KB pages selected between four primary slots through PPI port
  `0xA8`: firmware and C-BIOS logo ROMs in slot 0, a plain cartridge in slot
  1, an open slot 2, and up to 64 KB of RAM in slot 3.
- TMS9918/TMS9929 VRAM data/control ports, register and status behaviour,
  vertical interrupts, and MSX1 Text, Graphics I, Graphics II, and Multicolour
  rendering.
- TMS9918/TMS9929 sprite-mode-1 rendering with 8x8 and 16x16 patterns,
  magnification, early clock, priority, transparency, Y wrapping, the
  four-sprites-per-line limit, fifth-sprite index, and collision status.
- Complete 11-row international MSX keyboard matrix through PPI ports
  `0xA9`/`0xAA`, including modifiers, function and editing keys, the numeric
  keypad, simultaneous-key rollover, host-key aliases, and focus-loss cleanup.
- Minimal cassette-PPI and PSG register surfaces sufficient for the firmware
  boot path. Audio generation is not connected yet.
- Explicit `--bios`, `--logo`, and `--cart` loaders, plus a deterministic
  180-frame C-BIOS checkpoint below SDL.
- Reserved firmware, Nextor-kernel, Sunrise IDE, and raw hard-disk surfaces,
  clearly identified as unimplemented device and loader stubs.
- On-screen and console notifications, screenshots, pause, reset, and
  placeholders for capture and monitor tools.
- Generic MSX1 and MSX2 profiles that establish the VDP, slot, memory
  mapper, RTC, RAM, and VRAM boundaries.
- Component tests, an optional C-BIOS boot fixture, headless execution, and a
  machine-state dump for smoke testing.

Media rows and extension switches are intentionally labelled as stubs in the
interface until the corresponding devices exist.

## Building

A C11 compiler, GNU Autotools, `pkg-config`, SDL3 development files, and
`libm` are required. On Fedora, for example:

```sh
sudo dnf install gcc make autoconf automake pkgconf-pkg-config SDL3-devel
```

Then build and test:

```sh
autoreconf -iv
./configure
make -j4
make check
./1983
```

Useful development invocations include:

```sh
./1983 --model msx2 --region ntsc
./1983 --config ./test.conf
./1983 --headless --unthrottled --exit-after 10
./1983 --help
```

### Keyboard

SDL scancodes map positionally onto the standard international MSX keyboard.
Left or right Shift produces MSX Shift, left Ctrl produces CTRL, left Alt
produces GRAPH, right Alt produces CODE, and right Ctrl produces the
ACC/dead-key position. The editing keys, arrows, and numeric keypad map to
their corresponding MSX matrix positions; keypad Enter follows the common
MSX keypad-comma convention.

Unmodified function keys remain available to the shared emulator interface.
Use Shift+F1 through Shift+F5 for the MSX function keys, Shift+F7 for SELECT,
and Shift+F8 for STOP. The host Shift used for these chords is suppressed
from the guest matrix, so the MSX receives the intended key by itself. Opening
the overlay or moving focus away from the window releases every guest key.

### Booting C-BIOS

1983 does not currently bundle firmware. Download C-BIOS 0.29 from the
[C-BIOS project](https://cbios.sourceforge.net/), then start the generic
60 Hz MSX1 machine with its main and logo ROMs:

```sh
./1983 --region ntsc \
  --bios /path/to/cbios_main_msx1.rom \
  --logo /path/to/cbios_logo_msx1.rom
```

Add a plain cartridge of up to 64 KB with:

```sh
./1983 --region ntsc \
  --bios /path/to/cbios_main_msx1.rom \
  --logo /path/to/cbios_logo_msx1.rom \
  --cart /path/to/game.rom
```

Only linear ROM cartridges are implemented so far; ASCII and Konami mapper
cartridges will not yet run correctly. C-BIOS itself runs cartridge software
but does not provide MSX BASIC, cassette, or disk services. Use a legitimately
obtained vendor BIOS/BASIC image when those paths become implemented.

The current command line deliberately uses explicit firmware paths. The
planned Philips NMS 8250 profile will first search the user's existing
openMSX ROM pool at `~/.openMSX/share/systemroms`, then any configured
additional roots. It will search recursively, identify known ROM contents
primarily by checksum rather than filename, and report missing profile
components clearly. Vendor system ROMs will remain user-provided and will
not be copied into this repository.
This follows the practical and legal separation described by the
[openMSX system-ROM guide](https://openmsx.org/manual/setup.html#systemroms).

The reproducible headless firmware checkpoint is:

```sh
./1983 --config /dev/null --headless --unthrottled --region ntsc \
  --bios /path/to/cbios_main_msx1.rom \
  --logo /path/to/cbios_logo_msx1.rom \
  --exit-after 179 --dump-state
```

With the C-BIOS 0.29 images used by openMSX 21.0, this reaches frame 180 at
`PC=1A65`, `SP=F300`, and primary-slot register `F0`, with 5,692 non-zero
VRAM bytes. The optional fixture test exercises both that no-cartridge state
and C-BIOS launching a small synthetic cartridge:

```sh
MSX_CBIOS_DIR=/path/to/cbios make check
```

## Keyboard controls

| Key | Action |
|-----|--------|
| F4 | Save a PPM screenshot |
| F5 | Reset the selected machine profile |
| F6 | Animated capture placeholder |
| F8 | Monitor/disassembler placeholder |
| F9 | Open or save and close the options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Pause | Pause or resume |
| Ctrl++ / Ctrl+- | Increase or decrease window scale |

Inside the overlay, Left and Right change section, Up and Down select a row,
Enter changes the selected setting, F9 applies and saves, and Escape offers to
save or discard modified settings. The Advanced section appears after enabling
General > Tinker.

Configuration is saved to `~/.config/1983/1983.conf` on Unix-like systems and
under the user's application-data directory on Windows. Pass `--config PATH`
to use an isolated file. See [`1983.conf.example`](1983.conf.example) for the
currently supported settings.

## Goals

- Run the broadest practical range of MSX and MSX2 games, demos, applications,
  and system software without being tied to a single vendor's model.
- Model the standard hardware accurately enough for software that depends on
  video, interrupt, slot, memory, and I/O timing.
- Represent real machine differences through selectable profiles and
  configuration instead of title-specific compatibility hacks.
- Make cartridge, cassette, and disk software straightforward to load, with
  sensible automatic detection and explicit overrides when detection is
  ambiguous.
- Reuse the familiar overlays, shortcuts, media workflows, display controls,
  capture tools, and development automation of 1984 and 1985.
- Remain multiplatform through portable C and SDL3, sharing improvements across
  the sibling projects wherever possible.

## Planned baseline

| Area | Intended support |
|------|------------------|
| CPU | Z80 instruction set, interrupts, and cycle-aware execution (initial core integrated) |
| Machine architecture | Primary slots and linear ROM/RAM devices implemented; secondary slots and memory mappers planned |
| MSX video | TMS9918-family pattern modes, sprite mode 1, status flags, limits, collisions, and interrupts implemented; cycle-level timing refinement planned |
| MSX2 video | V9938 display modes, palettes, sprites, scrolling, expanded VRAM, and interrupts |
| Audio | AY-3-8910-compatible PSG, with SCC and MSX-MUSIC as compatibility extensions |
| Cartridges | Plain ROMs and common ASCII, Konami, and Konami SCC mapper families, with mapper override controls |
| Cassette | CAS images and the standard BIOS cassette path |
| Disk | DSK images, common MSX disk-ROM behaviour, Sunrise ATA-IDE, Nextor-compatible block storage, guest writes, and multiple drives |
| Input | International MSX keyboard matrix implemented; joysticks, mouse, alternate national layouts, and host clipboard paste planned |
| MSX2 hardware | RAM memory mapper, real-time clock, and model-appropriate firmware configuration |
| Tools | Debugger and disassembler, screenshots, snapshots, headless execution, and deterministic automation |

The first compatibility target is standard MSX1 software. The first
mass-storage boot target matches GeoBench's openMSX setup: a PAL Philips
NMS 8250 with its internal 128 KB mapper, a separate 512 KB mapper
extension, and Nextor 2.1.1 through an emulated Sunrise ATA-IDE cartridge.
MSX2+ and MSX turbo R are not part of the initial scope, although the design
should leave room for later machine generations.

## Compatibility approach

MSX is a standard implemented by many machines rather than one fixed hardware
configuration. 1983 therefore separates the shared architecture from
machine-specific choices such as region, video frequency, BIOS set, slot
layout, RAM, VRAM, and built-in extensions.

Cartridge mapper detection will prefer safe heuristics while retaining a
manual override. Compatibility work will be backed by repeatable boot,
framebuffer, audio, and timing tests so that support for one machine profile
does not silently break another.

## Roadmap

1. **Frontend scaffold:** portable build, SDL3 window and display, persistent
   configuration, overlays, notifications, LEDs, machine profiles, and smoke
   testing. This initial step is in place.
2. **Executable MSX1 slice:** integrate the sibling Z80 core, primary-slot
   bus, PPI slot control, firmware/plain-cartridge loading, and the initial
   TMS9918/TMS9929 video path. C-BIOS now reaches a deterministic boot
   checkpoint and launches a test cartridge.
3. Refine TMS9918/TMS9929 timing, generate PSG audio, add common cartridge
   mappers, cassette support, joysticks, alternate national keyboard layouts,
   a supplied BIOS/BASIC checkpoint, and an MSX1 compatibility suite.
4. Implement the V9938, MSX2 secondary slots, multiple memory mappers, RTC,
   and the Philips NMS 8250 reference profile; then boot its user-supplied
   Nextor 2.1.1 Sunrise IDE ROM and the same raw hard-disk image used by
   `../geobench/tools/run_msx.sh`.
5. Add commonly required sound and cartridge extensions, improve timing
   accuracy, and expand regression coverage.
6. Package tested releases for the host platforms supported by 1984 and 1985.

## Shared interface and multiplatform design

Being a sibling project is part of 1983's design, not just its name. It reuses
the interface language of 1984 and 1985: the fixed guest canvas, status footer,
LED strip, F9 overlay, function-key workflow, notifications, display controls,
headless execution, and automation-friendly command line.

The MSX emulation remains behind a narrow machine boundary so the slot
architecture, VDPs, firmware profiles, and peripherals can be modelled on
their own terms. SDL3 supplies the portable window, renderer, input, and
event layer. See [`DEVELOPMENT.md`](DEVELOPMENT.md) for the initial source
layout and hardware assumptions.

The local openMSX 21.0 source and machine definitions were used as an
implementation reference to cross-check MSX ports, slot behaviour, and
representative MSX1/MSX2 configurations. openMSX remains an independent
project; 1983 is not intended to reproduce its architecture.

## Firmware and software

1983 will support both C-BIOS and user-supplied MSX firmware. C-BIOS provides
a redistributable cartridge-oriented default, but it does not provide BASIC,
cassette, or disk support. A supplied BIOS/BASIC set therefore remains
necessary for representative BASIC and peripheral compatibility.

[Nextor](https://github.com/Konamiman/Nextor) is the first disk operating
system target for configurations with supported storage hardware. It is a
guest OS rather than an MSX BIOS replacement: reaching its command prompt
requires a matching kernel ROM, a storage-controller implementation, and a
boot volume containing `NEXTOR.SYS` and `COMMAND2.COM`.

MSX BIOS, BASIC, disk ROMs, cartridge images, and other software may be
copyrighted. The project will only distribute firmware or test software when
its licence permits redistribution. Users are responsible for supplying any
other required images from hardware or media they are entitled to use. See
[`BOOT_TARGETS.md`](BOOT_TARGETS.md) for the selected boot lanes, first
Nextor configuration, checkpoints, and distribution boundaries.

## Contributing

Hardware documentation, small redistributable test programs, timing traces,
and well-described compatibility cases are especially useful. Please use the
[issue tracker](https://github.com/salvogendut/1983/issues) to discuss a
machine profile, device, or implementation change before starting substantial
work.

## License

1983 is free software released under the GNU General Public License,
version 2.0 only (`GPL-2.0-only`). See [`LICENSE`](LICENSE).
