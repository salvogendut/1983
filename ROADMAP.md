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
| Machine architecture | Primary slots, NMS 8250 expanded slots, its internal mapper, configurable mapper capacity through 4 MiB, and the SD Mapper V2's independent 512 KiB mapper are implemented |
| MSX video | Pattern modes, sprite mode 1, status, collisions, and interrupts are implemented; timing refinement remains |
| MSX2 video | V9938 registers, palette, bitmap modes, sprite mode 2, commands, interrupts, contended VRAM, and scanline-progressive output are implemented |
| Audio | AY/YM PSG and SDL3 output are implemented; SCC and MSX-MUSIC are planned |
| Cartridges | Dual Linear, ASCII8/16, Konami, and Konami SCC devices with persistent mapper controls are implemented |
| Cassette | Standard CAS playback, BIOS motor/comparator wiring, overlay/CLI loading, transport display, and Tape LED are implemented; recording and sampled audio are planned |
| Disk | Sunrise ATA-IDE/Nextor, dual-card SD Mapper V2, and Philips WD2793 paths are implemented with safe writable raw images, optional dual floppies, and activity LEDs; protected/flux formats remain planned |
| Input | International keyboard, dual PSG joystick ports, SDL3 gamepad hotplug/routing, MSX mouse capture/protocols, and persistent Joy Port selections are implemented; alternate matrices and clipboard paste are planned |
| MSX2 hardware | Internal mapper, RP-5C01 RTC with persistent per-machine CMOS, and editable firmware catalogue are implemented; more extensions are planned |
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
8. Sunrise IDE cartridge and safe writable ATA backend, including an official
   Nextor/GeoBench boot checkpoint and GUI media workflow.
9. Standard MSX CAS playback with cycle-timed transport, firmware-visible
   motor/comparator signals, persistent GUI/CLI media workflow, and Tape LED.
10. Philips NMS 8250 WD2793 emulation with conventional raw DSK images,
    safe sector writes/ejection, and optional Floppy B.
11. Complete RP-5C01 control/test/calendar behavior with validated,
    atomically saved per-machine CMOS and offline clock continuity.
12. Composite MSX SD Mapper V2 cartridge with expanded subslots, dual SPI SD
    media, an independent 512 KiB mapper, safe image writes, and a real
    Nextor 2.1.2 boot checkpoint.

## Near-term targets

1. Implement MegaFlashROM SCC+ SD as the next composite storage/audio
   cartridge, reusing the SD image backend without conflating its flash,
   SCC+, mapper, and slot behavior with SD Mapper V2.
2. Add alternate national keyboard layouts and a small redistributable MSX1
   compatibility corpus; expand cassette support with recording or sampled
   audio after playback compatibility is established.
3. Refine progressive VDP rendering from completed-scanline changes to
   within-scanline fetch timing where software depends on raster effects.
4. Add deterministic snapshots and audio/video capture for compatibility
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

The Sunrise cartridge and reusable ATA task-file backend are implemented.
With the NMS 8250 internal disk ROM omitted, the official Nextor 2.1.1
Sunrise kernel boots the 32 MiB GeoBench image to its desktop on the stock
128 KiB internal mapper. Images default to read-only and can explicitly use
read/write access with flush and safe-ejection handling.

The separate 512 KiB mapper is now implemented as part of the real SD Mapper
V2 composite cartridge rather than being folded into a fictitious 640 KiB
internal mapper. That device also supplies two SD slots and boots the
official SDXC110 ROM with Nextor 2.1.2 from a FAT16 card image. The internal
WD2793 path boots conventional 720 KiB media; protected disk formats and
track-formatting commands remain later storage layers.

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
