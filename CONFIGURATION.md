# 1983 - Configuration

User settings are stored in `~/.config/1983/1983.conf` on Unix-like systems
and in the application-data directory on Windows. `--config PATH` selects an
isolated configuration. [`1983.conf.example`](1983.conf.example) documents the
available settings.

## RTC and CMOS persistence

RTC files follow the selected configuration file, so isolated configurations
also get isolated clocks. On Unix, `--config /dev/null` deliberately disables
RTC persistence for disposable, deterministic runs.

## Machine catalogue

`1983-models.conf` is searched for in the user configuration directory, the
current directory, and the installed application-data directory.
`--models PATH` selects a different catalogue. The graphical machine editor
always writes a per-user catalogue, seeding it with the complete active
catalogue on the first saved edit so installed or repository copies remain
untouched.

## Guest DOS files

The local `DOS/` directory is reserved for guest DOS files. Its contents,
including `NEXTOR.SYS`, are ignored and are not distributed with 1983.

## RS-232C serial interface

`rs232 = true` enables the MSX RS-232C interface on ports `80h-87h` (8251
USART + 8254 timer); on Linux/BSD the host side is a PTY at
`/tmp/1983-rs232`. Set `rs232_rom = /path/to/rs232.rom` to provide the
user-supplied MSX-serial driver ROM used for EXTBIO auto-detection
(`OPEN "COM0:"`). The ROM is loaded into a cartridge slot automatically; it
is not bundled with 1983. Without it, the port device still works but BASIC
auto-detect is unavailable.