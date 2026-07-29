# 1983 roadmap

1983 aims to run the broadest practical range of MSX and MSX2 games, demos,
applications, and system software while preserving the shared interface of
the 1984 and 1985 sibling emulators.

## Principles

- Model the standard hardware accurately enough for software which depends
  on video, interrupt, slot, memory, and I/O timing.
- Represent machine differences through profiles and configuration instead
  of title-specific compatibility hacks.
- Make cartridges, cassettes, and disks straightforward to load, with
  automatic detection and explicit overrides where necessary.
- Keep the emulator portable through C11, SDL3, and limited
  platform-specific code.
- Back compatibility work with deterministic component, boot, framebuffer,
  audio, and timing tests.

## Support plan

| Area | Current position and intended support |
|------|---------------------------------------|
| CPU | Z80 instruction set, interrupts, and cycle-aware execution are integrated |
| Machine architecture | Primary slots, NMS 8250 expanded slots, its internal mapper, and configurable mapper capacity through 4 MiB are implemented |
| MSX video | Pattern modes, sprite mode 1, status, collisions, and interrupts are implemented; timing refinement remains |
| MSX2 video | V9938 registers, palette, bitmap modes, sprite mode 2, commands, interrupts, contended VRAM, and scanline-progressive output are implemented |
| Audio | AY/YM PSG and SDL3 output are implemented; SCC and MSX-MUSIC are planned |
| Cartridges | Dual Linear, ASCII8/16, Konami, and Konami SCC devices with persistent mapper controls are implemented |
| Cassette | CAS images and the standard BIOS cassette path are planned |
| Disk | DSK images, WD2793 behavior, Sunrise ATA-IDE, guest writes, and multiple drives are planned |
| Input | International keyboard, dual PSG joystick ports, SDL3 gamepad hotplug/routing, and persistent Joy Port selections are implemented; mouse protocols, alternate matrices, and clipboard paste are planned |
| MSX2 hardware | Internal mapper, RTC, and editable firmware catalogue are implemented; persistent CMOS and more extensions are planned |
| Tools | Screenshots and headless automation exist; snapshots, debugger, disassembler, and deterministic capture are planned |

## Completed foundations

1. Portable Autotools/SDL3 frontend, configuration, overlays, LEDs,
   notifications, and headless smoke testing.
2. Executable MSX1 slice with Z80, primary-slot bus, PPI, firmware,
   cartridges, TMS9918-family video, keyboard, and PSG.
3. Common cartridge mapper framework for two independent slots.
4. MSX2 V9938 foundation, bitmap and sprite modes, command engine,
   beam-positioned status and interrupts, expanded slots, internal mapper,
   and RTC.
5. Scanline-progressive V9938 rendering and timed CPU/command VRAM access.
6. Editable machine catalogue, General > Machine firmware workflow, and the
   Advanced machine model editor.
7. Dual active-low MSX joystick ports with SDL3 gamepad input and live
   Main Input routing.

## Near-term targets

1. Add cassette loading, MSX mouse input, alternate national keyboard
   layouts, and a small redistributable MSX1 compatibility corpus.
2. Refine progressive VDP rendering from completed-scanline changes to
   within-scanline fetch timing where software depends on raster effects.
3. Add deterministic snapshots and audio/video capture for compatibility
   investigations.

## Storage and Nextor

The first mass-storage target matches `../geobench/tools/run_msx.sh`:

- PAL Philips NMS 8250;
- its internal 128 KiB mapper;
- a separate 512 KiB external mapper;
- Sunrise ATA-IDE cartridge;
- official Nextor 2.1.1 Sunrise kernel;
- a raw FAT16 hard-disk image containing `NEXTOR.SYS`, `COMMAND2.COM`, and
  GeoBench.

The external mapper remains a distinct device rather than being folded into
a fictitious 640 KiB internal mapper. The Sunrise wrapper should remain
separate from a reusable ATA task-file backend.

The staged storage checkpoints are defined in
[`BOOT_TARGETS.md`](BOOT_TARGETS.md).

## Later work

- Broaden MSX1 and MSX2 compatibility and regression corpora.
- Add SCC and MSX-MUSIC sound.
- Add commonly required cartridge and machine extensions.
- Improve VDP, audio, and bus timing where real software exposes a
  difference.
- Package tested releases for the host platforms supported by 1984 and 1985.

MSX2+ and MSX turbo R are outside the initial scope, but the catalogue and
machine boundaries should leave room for later generations.
