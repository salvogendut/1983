# 1983 - MSX / MSX2 emulator

1983 is a compatibility-focused MSX and MSX2 emulator written in C with
SDL3. It aims to run software for the first two generations of the MSX
standard without being tied to one manufacturer's machine.

It is the sibling project of
[1984](https://github.com/salvogendut/1984), the Amstrad CPC emulator, and
[1985](https://github.com/salvogendut/1985), the Amstrad PCW emulator. It
shares their window, overlay, keyboard shortcut, notification, LED, and
configuration conventions, and is intended to remain as multiplatform as
SDL3 allows.

> **Status:** MSX1 and MSX2 firmware and cartridge software are running.
> The emulator includes TMS9918-family and V9938 video, AY/YM PSG audio,
> the international keyboard matrix, dual cartridge slots with common
> mappers, MSX2 expanded slots, memory mapper, and RTC. Cassette, floppy,
> Sunrise IDE, Nextor boot storage, SCC audio, and other extensions remain
> in development.

## Highlights

- MSX, MSX2, and Philips NMS 8250 machine layouts.
- Editable `1983-models.conf` machine and firmware catalogue.
- Linear, ASCII8, ASCII16, Konami, and Konami SCC cartridges.
- MSX1 screen and sprite modes plus V9938 SCREEN 5 through SCREEN 8,
  sprite mode 2, drawing commands, interrupts, and timed VRAM access.
- Cycle-timed AY-3-8910/YM2149 audio through SDL3.
- Complete international MSX keyboard matrix.
- Familiar F9 overlay, function-key controls, status LEDs, notifications,
  screenshots, fullscreen, and integer scaling.
- Headless execution and deterministic component and firmware tests.

The detailed implementation and remaining limitations are recorded in
[`TECHNICAL.md`](TECHNICAL.md).

## Building

1983 requires a C11 compiler, GNU Autotools, `pkg-config`, SDL3 development
files, and `libm`. On Fedora:

```sh
sudo dnf install gcc make autoconf automake pkgconf-pkg-config SDL3-devel
```

Build, test, and run:

```sh
autoreconf -iv
./configure
make -j4
make check
./1983
```

Useful options:

```sh
./1983 --model msx2 --region pal
./1983 --cart ROMS/game.rom
./1983 --models ./my-models.conf --model my-msx
./1983 --config ./test.conf
./1983 --headless --unthrottled --exit-after 10
./1983 --help
```

## Firmware and machines

1983 does not bundle copyrighted MSX system ROMs. Place firmware and
cartridges you are entitled to use in the Git-ignored `ROMS/` directory, or
point the machine catalogue at another private location.

[`1983-models.conf`](1983-models.conf) initially defines:

- `msx1` — MSX with a 32 KiB BIOS;
- `msx2` — MSX2 with a BIOS and Sub-ROM;
- `nms8250` — Philips NMS 8250 with BIOS, Sub-ROM, and disk ROM.

Open the F9 overlay and select **General > Machine**. Complete catalogue
mappings load directly; missing required components open file pickers. The
whole firmware set is validated before replacing the running machine.

To add another model using an implemented hardware layout, copy a catalogue
section, give it a unique ID and name, and change its firmware paths. Paths
may be absolute or relative to the catalogue file.

For graphical editing, enable **General > Tinker**, then open
**Advanced > Machine model editor**. It can add, edit, duplicate, and delete
models; choose firmware files; validate IDs and ROM sizes; and atomically
save the complete catalogue to the user configuration directory. Select the
edited model under **General > Machine** to apply it.

C-BIOS can be started explicitly:

```sh
./1983 --region ntsc \
  --bios /path/to/cbios_main_msx1.rom \
  --logo /path/to/cbios_logo_msx1.rom \
  --cart /path/to/game.rom
```

With the default local mappings and user-supplied ROMs, start the NMS 8250
with:

```sh
./1983 --model nms8250 --region pal
```

See [`BOOT_TARGETS.md`](BOOT_TARGETS.md) for firmware setup, diagnostic
checkpoints, the GeoBench/Nextor target, and licensing boundaries.

## Controls

| Key | Action |
|-----|--------|
| F4 | Save a PPM screenshot |
| F5 | Reset |
| F6 | Animated capture placeholder |
| F8 | Monitor/disassembler placeholder |
| F9 | Open or save and close the options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Pause | Pause or resume |
| Ctrl++ / Ctrl+- | Change window scale |
| Shift+F1…F5 | Send MSX F1…F5 |
| Shift+F7 / Shift+F8 | Send MSX SELECT / STOP |

Inside the overlay, Left and Right change section, Up and Down select a row,
Enter activates it, F9 saves, and Escape closes or offers to discard changes.
The Advanced section appears after enabling **General > Tinker**.

SDL scancodes map positionally to the international MSX keyboard. Left Ctrl
is CTRL, left Alt is GRAPH, right Alt is CODE, and right Ctrl is the ACC/dead
key. Both Shift keys, editing keys, arrows, and the numeric keypad are
supported.

## Configuration

User settings are stored in `~/.config/1983/1983.conf` on Unix-like systems
and in the application-data directory on Windows. Use `--config PATH` for an
isolated configuration; [`1983.conf.example`](1983.conf.example) documents
the available settings.

1983 searches for `1983-models.conf` in the user configuration directory,
the current directory, and the installed application-data directory.
`--models PATH` selects a different catalogue. The graphical editor always
writes a per-user catalogue, seeding it with the complete active catalogue
on the first saved edit so installed or repository copies remain untouched.

The local `DOS/` directory is reserved for guest DOS files. Only the
unmodified `DOS/NEXTOR.SYS` is tracked; other DOS files are ignored.

## Documentation

- [`TECHNICAL.md`](TECHNICAL.md) — implemented hardware, timing, media, and
  frontend behavior.
- [`DEVELOPMENT.md`](DEVELOPMENT.md) — source layout, design boundaries,
  hardware notes, and verification.
- [`ROADMAP.md`](ROADMAP.md) — project goals, support matrix, and planned
  work.
- [`BOOT_TARGETS.md`](BOOT_TARGETS.md) — firmware lanes, reproducible
  checkpoints, Nextor target, and licensing.

## Contributing

Hardware documentation, redistributable test programs, timing traces, and
well-described compatibility cases are welcome. Please use the
[issue tracker](https://github.com/salvogendut/1983/issues) before starting a
substantial machine, device, or architecture change.

## License

1983 is released under the GNU General Public License version 2.0 only
(`GPL-2.0-only`). See [`LICENSE`](LICENSE).
