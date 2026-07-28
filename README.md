# 1983 - MSX / MSX2 emulator

1983 is a compatibility-focused emulator for MSX and MSX2 computers. Rather
than reproducing one particular manufacturer's machine, it will provide a
generic, configurable implementation of the MSX architecture with the goal of
running most software written for the first two generations of the standard.

It is a sibling project of [1984](https://github.com/salvogendut/1984), the
Amstrad CPC emulator, and [1985](https://github.com/salvogendut/1985), the
Amstrad PCW emulator. The name marks 1983, the year in which the MSX standard
was introduced.

Like its siblings, 1983 will be written in C using SDL3. It will reuse their
established interface, options overlays, host integration, and development
tools so that the three emulators look and behave as members of the same
family. Portable code and minimal platform-specific dependencies will keep it
available on as many SDL3-supported systems as practical.

> **Project status:** 1983 is at the initial development stage. There is no
> usable emulator or release yet; the features below describe the intended
> scope.

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
| Disk | DSK images, common MSX disk-ROM behaviour, guest writes, and multiple drives |
| Input | MSX keyboard matrix, joysticks, mouse, and host clipboard paste |
| MSX2 hardware | RAM memory mapper, real-time clock, and model-appropriate firmware configuration |
| Tools | Debugger and disassembler, screenshots, snapshots, headless execution, and deterministic automation |

The first compatibility target is standard MSX1 software, followed by the
MSX2 video, memory, clock, and disk environment. MSX2+ and MSX turbo R are not
part of the initial scope, although the design should leave room for later
machine generations.

## Compatibility approach

MSX is a standard implemented by many machines rather than one fixed hardware
configuration. 1983 will therefore separate the shared architecture from
machine-specific choices such as region, video frequency, BIOS set, slot
layout, RAM, VRAM, and built-in extensions.

Cartridge mapper detection will prefer safe heuristics while retaining a
manual override. Compatibility work will be backed by repeatable boot,
framebuffer, audio, and timing tests so that support for one machine profile
does not silently break another.

## Roadmap

1. Establish the portable C codebase by adapting the shared Z80, build, SDL3
   display, audio, input, overlay, and automation foundations.
2. Boot an MSX1 BIOS and BASIC with working slots, keyboard, video, and PSG
   audio.
3. Add cartridge mappers, cassette support, joysticks, and a representative
   MSX1 compatibility suite.
4. Implement the V9938, MSX2 memory and RTC hardware, disk interfaces, and
   MSX2 machine profiles.
5. Add commonly required sound and cartridge extensions, improve timing
   accuracy, and expand regression coverage.
6. Package tested releases for the host platforms supported by 1984 and 1985.

## Shared interface and multiplatform design

Being a sibling project is part of 1983's design, not just its name. It will
reuse proven components from 1984 and 1985 where doing so improves consistency:
the Z80 core, SDL3 host layer, build system, configuration UI, debugger, media
handling, capture tools, and automated test interfaces.

The familiar F9 options overlay and its navigation patterns will carry over,
including model and media controls, extension settings, display and audio
options, and an advanced area for debugging and automation. Common keyboard
shortcuts and file-selection workflows will also remain consistent across the
three emulators wherever the guest hardware permits.

SDL3 will provide the window, renderer, audio, keyboard, mouse, and controller
host layer. Platform-specific integrations will be kept separate from the
emulation core, with the aim of supporting the same broad range of Linux,
Windows, macOS, BSD, and other SDL3-capable environments as the sibling
projects whenever their toolchains and required libraries allow it.

1983 will remain a separate emulator so that the MSX slot architecture, VDPs,
firmware profiles, and peripherals can be modelled on their own terms while
shared frontend improvements can flow between all three projects.

## Firmware and software

MSX BIOS, BASIC, disk ROMs, cartridge images, and other software may be
copyrighted. The project will only distribute firmware or test software when
its licence permits redistribution. Users are responsible for supplying any
other required images from hardware or media they are entitled to use.

## Contributing

The project is currently defining its foundations. Hardware documentation,
small redistributable test programs, timing traces, and well-described
compatibility cases are especially useful. Please use the
[issue tracker](https://github.com/salvogendut/1983/issues) to discuss a
machine profile, device, or implementation change before starting substantial
work.

Build instructions and a detailed compatibility list will be added once the
first runnable milestone is available.
