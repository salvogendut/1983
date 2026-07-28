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

> **Project status:** the initial frontend scaffold builds and runs, but it
> does not emulate a Z80, VDP, sound chip, or storage device yet. It currently
> provides the host interface and machine-profile boundaries on which the
> emulator will be built.

## Current scaffold

- Autotools-based C11 and SDL3 build, following the sibling projects.
- Resizable 640x480 guest display with a footer, LED bar, fullscreen and
  integer window scaling.
- F9 options overlay with General, Media, Extensions, and Advanced sections.
- Persistent generic MSX1/MSX2, PAL/NTSC, RAM, display, extension, and
  notification settings.
- Power, Caps, Kana, drive, and cassette LEDs with hover descriptions.
- Reserved firmware, Nextor-kernel, Sunrise IDE, and raw hard-disk surfaces,
  clearly identified as unimplemented device and loader stubs.
- On-screen and console notifications, screenshots, pause, reset, and
  placeholders for capture and monitor tools.
- Generic MSX1 and MSX2 profiles that establish the future VDP, slot, memory
  mapper, RTC, RAM, and VRAM boundaries.
- A small machine-profile test and a headless mode for smoke testing.

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
| CPU | Z80 instruction set, interrupts, and cycle-aware execution |
| Machine architecture | Primary and secondary slots, ROM and RAM layouts, memory mappers, and configurable regional profiles |
| MSX video | TMS9918-family display modes, sprites, status flags, and interrupts |
| MSX2 video | V9938 display modes, palettes, sprites, scrolling, expanded VRAM, and interrupts |
| Audio | AY-3-8910-compatible PSG, with SCC and MSX-MUSIC as compatibility extensions |
| Cartridges | Plain ROMs and common ASCII, Konami, and Konami SCC mapper families, with mapper override controls |
| Cassette | CAS images and the standard BIOS cassette path |
| Disk | DSK images, common MSX disk-ROM behaviour, Sunrise ATA-IDE, Nextor-compatible block storage, guest writes, and multiple drives |
| Input | MSX keyboard matrix, joysticks, mouse, and host clipboard paste |
| MSX2 hardware | RAM memory mapper, real-time clock, and model-appropriate firmware configuration |
| Tools | Debugger and disassembler, screenshots, snapshots, headless execution, and deterministic automation |

The first compatibility target is standard MSX1 software. The first
mass-storage boot target is Nextor on the generic MSX2 profile through an
emulated Sunrise ATA-IDE cartridge. MSX2+ and MSX turbo R are not part of the
initial scope, although the design should leave room for later machine
generations.

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
2. Integrate the sibling Z80 core, implement the PPI and slot-aware memory and
   I/O buses, and boot an MSX1 BIOS and BASIC.
3. Add the TMS9918/TMS9929 video path, keyboard matrix, PSG audio, cartridge
   mappers, cassette support, joysticks, and an MSX1 compatibility suite.
4. Implement the V9938, MSX2 secondary slots, memory mapper, RTC, and
   representative MSX2 machine profiles; then boot Nextor through a Sunrise
   ATA-IDE cartridge and raw hard-disk image.
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
