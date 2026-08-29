# 1983 technical overview

This document summarizes the hardware and frontend behavior currently
implemented by 1983. Low-level design notes and source ownership are in
[`DEVELOPMENT.md`](DEVELOPMENT.md).

## Machine architecture

1983 uses a cycle-budgeted Z80 with machine-owned memory and I/O callbacks.
Its address space is divided into four 16 KiB pages selected between four
primary slots through PPI port `0xA8`.

- Slot 0 contains the BIOS and optional C-BIOS logo ROM, or the first 64 KiB
  slice of an Omega unified ROM.
- Slots 1 and 2 are independent cartridge devices.
- Slot 3 contains ordinary MSX RAM or the expanded MSX2 devices.

The Philips NMS 8250 layout implements expanded primary slot 3, its inverted
secondary-slot register at `0xFFFF`, the MSX2 Sub-ROM, 128 KiB internal
mapper RAM, and RTC. Memory-mapper segment registers are exposed at ports
`0xFC` through `0xFF`. Floppy topology is supplied separately by the selected
catalogue entry, so the same Philips memory-mapped WD2793 and disk ROM can be
wired into either the NMS 8250 or a compatible generic layout.

An Omega unified ROM is exactly 512 KiB. Its lower and upper 256 KiB halves
represent the two physical EEPROM banks selected by Omega jumper JP1. The
selected half contains four consecutive 64 KiB slot images: primary slot 0,
expanded slots 3-0 and 3-1, and expanded slot 3-3. Slot 3-2 remains mapper
RAM. Loading this image is mutually exclusive with the individual BIOS, logo,
Sub-ROM, and disk-ROM paths; switching either way clears the inactive form
before resetting the guest. A configured WD2793 may still intercept its
register addresses within the embedded slot 3-3 disk-ROM page.

The General RAM control offers power-of-two mapper capacities through
4096 KiB (plus the smaller 16/32 KiB MSX1 layouts). Capacity above 128 KiB is
allocated on demand instead of enlarging the machine structure and host
stack. On MSX1, selecting more than 64 KiB enables the mapper port and
segment behavior needed to make the extra RAM addressable.

The current hardware layouts are:

| Setting | MSX | MSX2 | Philips NMS 8250 |
|---------|-----|------|------------------|
| CPU | Z80 at 3,579,545 Hz | Z80 at 3,579,545 Hz | Z80 at 3,579,545 Hz |
| Default RAM | 64 KiB | 128 KiB | 128 KiB |
| VRAM | 16 KiB | 128 KiB | 128 KiB |
| VDP | TMS9918A/TMS9929A | V9938 | V9938 |
| Expanded slots | No | Yes | Yes |
| Memory mapper | With RAM above 64 KiB | Yes | Yes |
| RTC | No | Yes | Yes |
| Firmware | BIOS | BIOS + Sub-ROM | BIOS + Sub-ROM; disk ROM optional |

## Machine catalogue

`1983-models.conf` separates user-visible model definitions from compiled
hardware layouts. Each `[model id]` entry accepts:

```ini
[model my-msx2]
name = My MSX2
hardware = msx2
unified_rom =
unified_rom_bank = 0
bios = ROMS/my-bios.rom
logo =
subrom = ROMS/my-subrom.rom
disk_rom =
floppy_controller = none
floppy_primary_slot =
floppy_secondary_slot =
```

For an Omega-style image, set `unified_rom` to an exact 512 KiB file and use
`unified_rom_bank = 0` for its lower half or `1` for its upper half. Leave
`bios`, `logo`, `subrom`, and `disk_rom` blank. A non-empty unified path wins
when an older hand-edited catalogue supplies both forms, and the editor makes
the choices mutually exclusive.

`hardware` must currently be `msx1`, `msx2`, or `nms8250`. A catalogue may
contain up to 64 valid entries. Duplicate IDs and unknown hardware layouts
are ignored, relative firmware paths are resolved from the catalogue
directory, and built-in entries remain available if no valid catalogue can
be loaded.

General > Machine enumerates the catalogue rather than a compiled model
count. It validates and atomically loads the selected definition exactly as
saved, without opening firmware file pickers. Blank optional fields remain
disconnected; missing or invalid required firmware preserves the previous
machine and reports that the definition must be fixed in the model editor.
The catalogue also validates controller/disk-ROM pairing and slot conflicts.
The supported `philips-wd2793` controller may occupy external primary slot 1
or 2, or free secondary slot 1/3 beneath expanded primary slot 3. An external
mapping reserves the corresponding physical cartridge port.

With Tinker enabled, Advanced > Machine model editor provides catalogue
list, add, edit, duplicate, and delete workflows. IDs are restricted to
portable INI-safe characters and must be unique. Non-empty firmware paths
selected or changed in the editor are checked for their required 512 KiB,
32 KiB, or 16 KiB size. Empty optional fields remain valid and leave those
components disconnected when the model is selected.

Editor changes are written through a same-directory temporary file and
atomically renamed to the per-user `1983-models.conf`. The saved file is
reloaded before replacing the active in-memory catalogue. This leaves the
last valid catalogue active if writing or parsing fails and avoids modifying
read-only packaged data.

The selected model ID, resolved hardware layout, and per-user firmware
overrides are stored in `1983.conf`. Explicit command-line options take
precedence.

## Input

General exposes a Main Input selection between Joy Port A and Joy Port B,
plus independent Joystick/Mouse device selections for both connectors.
These choices are persisted in `1983.conf`. The first SDL3 gamepad is routed
to Main Input when that connector is set to Joystick. Its D-pad and left
stick drive the four directions; south and east drive triggers A and B.
Hot-plug removal clears the input, and another available gamepad is selected
automatically. A connector set to Mouse receives relative motion and its two
buttons after the host pointer is captured by clicking the emulator window.

## Cartridges

Both external primary slots support:

- Linear ROMs;
- ASCII8 and ASCII16;
- Konami;
- Konami SCC.

Bank registers reset with the machine, banks wrap safely, and automatic
detection uses conservative mapper-write signatures. Each slot also has a
persistent manual override. Konami SCC cartridges use the shared five-channel
SCC waveform core and mix their output with the machine PSG.

The Media overlay can load, eject, and independently configure both
cartridges, the cassette, the active floppy drives, and media belonging to
connected storage extensions. The IDE hard-disk selector appears only when
Sunrise IDE is connected; device-specific SD Card A and SD Card B rows appear
only when SD Mapper V2 or MegaFlashROM SCC+ SD is connected. Nextor controller
kernels are cartridge firmware and
therefore have no separate Media row.

General > Extra Hardware reveals the Extensions section. Sunrise IDE, SD
Mapper V2, MegaFlashROM SCC+ SD, PowerGraph V9990, SCC, and MSX-MUSIC are
treated as cartridge-connected devices:
the first enabled device reserves cartridge slot 1 and the second reserves
slot 2. A catalogue floppy controller mapped to slot 1 or 2 reserves that
exact port before extensions are assigned; a configuration exceeding the
remaining capacity is refused. Mounting, ejecting, mapper changes, asynchronous
picker completion, and command-line startup all honor the same reservation
state. Enabling an extension ejects and forgets media in the newly reserved
slot.

The TCP/IP UNAPI host bridge is port-mapped, so it does not participate in
that reservation policy and can coexist with two occupied cartridge slots.

### PowerGraph V9990

**Extensions > PowerGraph V9990** models the GFX9000-compatible expansion as
a separate video cartridge rather than replacing the machine's internal VDP.
The General VDP selector continues to configure the internal
TMS9918/V9938/V9958 used by the BIOS. **PowerGraph output** models the choice a
real two-monitor or switched-input setup provides: `MSX VDP` and `V9990` force
one connector, while the default `Auto` source shows the internal boot display
until V9990 software enables its display output. The extension does not require
Tinker, reserves one physical cartridge slot, and exposes its 512 KiB VRAM,
register, palette, command, status, interrupt, and system-control interfaces at
ports `60h` through `6Fh`.

The initial renderer covers P1/P2 pattern modes and B0/B1/B2/B3/B4/B7 bitmap
modes with 2-, 4-, 8-, and 16-bit pixels, palette and direct-color output,
P1/P2 sprites, bitmap-mode hardware cursors, basic logical drawing commands,
and vertical/command interrupt signaling. Full command timing and exact
YJK/YUV conversion remain compatibility work. SYMG9K booting SymbOS with its
V9990 driver is the end-to-end compatibility target in addition to
deterministic component tests.
Run the opt-in native acceptance check with a user-supplied partitioned image
that contains `\SYMBOS\SYMG9K.COM` and `SYMG9K.BIN`:

```sh
make check-v9990-symbos SYMBOS_IMAGE=/path/to/symbos.img
```

The target works on a temporary copy, installs a dedicated `AUTOEXEC.BAT`,
boots the bundled Omega/RainBIOS and Sunrise firmware, and validates both the
captured PowerGraph desktop and the SymbOS hardware mouse cursor. The default
FAT partition begins at sector 32;
override `SYMBOS_IMAGE_OFFSET` when an image uses a different byte offset.
The installed startup file runs `CD \SYMBOS` followed by `SYMG9K`, so no
manual keyboard timing is involved in the acceptance result.

The footer always shows Cartridge I and Cartridge II indicators between
Power and Caps Lock. An occupied ROM slot or a slot owned by Sunrise IDE,
SD Mapper V2, or MegaFlashROM is orange. IDE reads use the dedicated Sunrise
IDE indicator; SD traffic uses independent green SD A and SD B indicators,
and TCP/IP bridge traffic uses a dedicated white network indicator.

## MSX TCP/IP UNAPI

The optional host network device is wire-compatible with
[openMSXnet v0.9.7](https://github.com/antxiko/openMSXnet). It claims I/O
ports `0x28` and `0x29`, answers the v1 `0xAB` detection handshake and bridge
version 4 capability response, and implements the byte-exact private protocol
used by the unmodified `UNAPINET.COM` guest driver. The bridge supplies
asynchronous IPv4 DNS, four active or passive TCP connections with bounded
send and receive buffers, and four UDP endpoints with bounded datagram
queues. IPv4 addresses use network byte order while ports, lengths, and
handles use the layouts defined by openMSXnet v1.

`UNAPINET.COM` is a separate TSR which exposes standard TCP/IP UNAPI 1.1 to
MSX programs through EXTBIO. It requires an MSX-DOS 2-compatible environment
and memory mapper, normally supplied by Nextor. The driver is not firmware,
is not a cartridge ROM, and is not bundled with 1983; use the v0.9.7 binary
from openMSXnet's releases and place it on the guest disk. GeoBench and SymbOS
then use their ordinary TCP/IP UNAPI clients without emulator-specific code.

Extensions > MSX TCP/IP UNAPI and the `--unapi` option control the same
persistent `tcpip_unapi` setting. Disabling it, resetting the MSX, or exiting
1983 closes every socket and cancels pending DNS state. The extension never
reserves a cartridge slot. ICMP echo follows openMSXnet's non-Windows v1
behavior: requests are acknowledged but no reply is reported; TCP and UDP
are fully available on every supported host.

Enabling the bridge grants the guest access to the host's IPv4 networking.
Outgoing TCP/UDP connections and passive TCP listeners therefore cross the
emulator boundary and are subject to the host firewall. Passive listeners
bind to all host interfaces, traffic is not encrypted by the bridge, and
untrusted guest software should be treated like an untrusted native network
client. The Flatpak package requests network sharing explicitly.

## Cassette

The host-independent cassette device accepts standard MSX `.cas` byte
streams. It recognizes the eight-byte CAS marker, groups ASCII, binary, and
BASIC blocks using their standard ten-byte type headers, and converts them
to the 14,976 Hz waveform used by the BIOS cassette routines. A zero bit is
encoded as one 3,744 Hz cycle, a one as two 7,488 Hz cycles, with LSB-first
data, one start bit, two stop bits, and the standard long/short leaders and
silences.

PPI port C bit 4 controls the active-low cassette motor and bit 5 carries
the output signal. PSG register 14 bit 7 reads the waveform comparator.
Transport advances against emulated Z80 time only while the motor is on, so
headless and unthrottled runs remain deterministic. Reset stops the motor
without ejecting or rewinding the tape.

Media > Cassette loads a `.cas` file atomically, retains the first file's
ASCII, binary, or tokenized BASIC type, displays stopped/playing/end state
and elapsed time, supports R to rewind and Delete to eject, and persists the
selected path. The detected type selects the `RUN"CAS:"`, `BLOAD"CAS:",R`,
or `CLOAD` followed by `RUN` hint. `--cassette PATH` provides the same
startup mount.

The Tape LED reports guest motor activity. Tinker exposes independent
audible and visual monitor settings in Advanced. The audible path samples
the synthesized waveform in emulated CPU time and mixes it into the
44.1 kHz PSG stream. The visual monitor copies the waveform immediately
behind the tape head into a translucent oscilloscope with transport time and
command guidance. Recording, WAV input, and seeking are not implemented.

## Configurable Philips WD2793 and floppy storage

The model catalogue maps the WD2793 command/status, track,
sector, and data registers at offsets `3FF8` through `3FFB`, mirrored in all
four 16 KiB pages. Offsets `3FFC` and `3FFD` select side, drive, and motor;
`3FFF` exposes the active-low IRQ and DRQ lines expected by the Philips disk
ROM. Its 16 KiB disk ROM is visible in page 1 of the same selected
slot/subslot. The stock NMS 8250 uses expanded slot 3-3, but generic MSX2
definitions can choose the same mapping and MSX1 definitions can connect the
device through primary cartridge slot 1 or 2. The controller implements
restore, seek and step operations, single and
multiple sector reads/writes, read address, and force interrupt. Reset
discards an incomplete transfer but preserves inserted media and completed
dirty sectors. A mounted drive with its motor running generates a cycle-based
300 rpm index pulse in Type-I status, as required by real CDX-2 firmware.

The host-independent raw DSK backend accepts conventional 160, 180, 320, 360,
640, and 720 KiB sector images. It prefers FAT BPB geometry when valid and
otherwise uses known MSX geometries. Images default to read-only. Explicit
read/write mode buffers a complete 512-byte sector before changing the host
file.
Replacement and ejection flush completed sectors; a host flush error leaves
the dirty image attached and visible instead of claiming a successful
ejection.

Media exposes Floppy A when the active model has a configured controller;
models without one reject floppy mounting. With Extra Hardware enabled,
Extensions > Second floppy adds Floppy B and its independent image selector.
The Philips drive register chooses between the two devices. Advanced >
Floppy access mode applies the explicit read-only/read-write policy to
inserted floppies. Paths, second-drive state, and access mode persist in
`1983.conf`; `--disk-a`, `--disk-b`, and `--floppy-mode` provide the same
startup controls. Sector access pulses the matching floppy LED.

The optional Microsol CDX-2 uses the same WD2793 core through ports `D0h-D4h`.
It is represented as a real cartridge extension: a user-supplied 16 KiB ROM
reserves one cartridge slot, while D4 selects drive/side/motor and reports
IRQ/DRQ. Clone boards can carry a 27C256 with two independent 16 KiB Disk ROMs;
their ROM_SW jumper ties EPROM A14 low or high. A 32 KiB image therefore
remains two fixed ROM halves rather than a software-banked cartridge, and the
emulated jumper selects one half before reset. The ROM is installed before
saved floppy media so a generic MSX can mount its startup disk without stale
capability gating.

The optional RDF600 is modelled as its own TDC-600-compatible cartridge. Its
exact 16 KiB, user-supplied Disk ROM occupies page 1. When its primary slot is
selected, TC8566AF main-status/data registers are mirrored through
`0000h-0FFFh` and `8000h-8FFFh`; writes in `1000h-1FFFh` and
`9000h-9FFFh` control drive selection and motors. The command engine implements
specify, seek/recalibrate, sense interrupt/drive status, read ID, sector
read/write, and format command/result phases. It shares the raw-image lifetime
and corruption-safety policy with the WD2793 controllers.

## Sunrise IDE and ATA storage

The Sunrise IDE extension is a real cartridge device rather than a generic
ROM mapper. It implements the 128 KiB, eight-bank ROM window, bit-reversed
bank control, IDE overlay enable, 16-bit data latch, task-file register
window, master/slave selection, alternate status, and device soft reset used
by the Sunrise interface. The address decode follows the openMSX 21.0
Sunrise implementation.

Its host-independent ATA backend exposes an LBA-capable device with IDENTIFY,
READ/WRITE SECTORS, multiple-sector transfers, FLUSH CACHE, diagnostic/reset
commands, geometry setup, and the feature commands needed by the official
Nextor 2.1.1 Sunrise kernel. Raw images must be a non-empty multiple of 512
bytes. They default to read-only; read/write access must be selected
explicitly. Incomplete writes stay in memory and reset discards them without
touching the image. Completed sectors are flushed before replacement,
ejection, and shutdown. A flush failure keeps the image mounted and is
reported by the GUI. Failed mounts preserve the previously mounted image.

On first activation, Extensions > Sunrise IDE opens a device-specific setup
panel which distinguishes the required 128 KiB controller ROM from its
optional raw disk image. Nothing is connected or reserved until Connect is
chosen and both selected files have been validated. The firmware path is
then retained so later activations are simple disconnect/reconnect toggles;
Space reopens the prepopulated settings. Active edits prepare replacement
firmware and media, flush writable state, and swap atomically. Delete safely
disconnects the extension and clears its saved controller and disk settings.
Media > IDE hard disk owns subsequent mounting and ejection while the
controller is connected. Advanced > IDE access mode owns the explicit
read-only/read-write switch. Both paths and the access mode persist in
`1983.conf`; command-line equivalents are `--sunrise-rom`, `--ide`, and
`--ide-mode`. Sector reads and writes pulse the
dedicated Sunrise IDE indicator; the owning cartridge LED remains orange to
show physical presence.

The Sunrise reference run deliberately omits the internal disk ROM so the
external controller owns boot. The Sunrise kernel boots the 32 MiB GeoBench
FAT16 image through Nextor to the GeoBench desktop using the stock 128 KiB
mapper. Independently, the NMS 8250 disk ROM boots conventional floppy
images through its native WD2793 path.

## MSX SD Mapper V2

The SD Mapper V2 is modeled as the single composite cartridge described by
the open hardware project, not as an unrelated SD controller plus a generic
RAM expansion. With its mapper switch enabled, the cartridge makes its
physical primary slot expanded. Subslot 0 contains a 128 or 256 KiB
controller ROM and both SD interfaces; subslot 1 contains an independent
512 KiB memory mapper. Disabling the mapper switch leaves the controller as
an ordinary cartridge. The alternate-driver switch selects the second
128 KiB half of a 256 KiB ROM.

The controller uses two 16 KiB firmware bank registers. When firmware bank 1
selects bank 7, `0x7B00` through `0x7EFF` become the SPI data window,
`0x7FF0` reports card presence/change/write-protect state and selects either
or both cards, and `0x7FF1` exposes the 8-bit countdown timer clocked at
25 MHz / 256. The mapper
uses standard ports `0xFC` through `0xFF`, resets to segments `3,2,1,0`, and
shares those ports electrically with another mapper through the MSX
active-low, wired-AND read convention.

`src/sdcard.c` implements the SPI-mode commands needed by the reference
firmware and Nextor, including initialization, card identification, capacity,
single/multiple block reads, single/multiple block writes, application
commands, stop, and status. It supports conventional and high-capacity card
addressing. Command replies follow the two-transfer latency used by openMSX,
and raising chip select pauses rather than destroys an in-progress transfer;
both behaviours are required by native SymbOS mass-storage drivers. The card
backend is independent of the cartridge wrapper so the
same conservative image lifetime applies to both sockets: read-only by
default, complete 512-byte writes, explicit flush and host synchronization,
failed-mount preservation, failed-flush retention, and safe ejection.

The guided Extensions setup distinguishes controller firmware from removable
SD media. Once connected, Media owns card insertion/ejection, while Advanced
owns read-only/read-write access and the two cartridge switches. Configuration
and command-line startup preserve the same separation through
`sd_mapper_rom`, `sd_card_a`, `sd_card_b`, and `sd_image_mode`, or
`--sd-mapper-rom`, `--sd-a`, `--sd-b`, and `--sd-mode`.
Space reopens the setup without toggling the extension; active changes are
prepared and swapped only after current writable cards flush successfully.

The reference `SDXC110.ROM` from MSX SD Mapper V2 release 1.1.0 with Nextor
2.1.2 boots a FAT16 card to its command prompt on the generic MSX1 profile.
Neither the ROM nor the card image is distributed by 1983.

## MegaFlashROM SCC+ SD

MegaFlashROM SCC+ SD is modeled as one physical cartridge with an expanded
primary slot. Subslot 0 mirrors the 16 KiB recovery area, subslot 1 exposes
the 7,104 KiB MegaFlash region, subslot 2 contains the independent 512 KiB
memory mapper, and subslot 3 exposes the 1 MiB MegaSD region and two SPI SD
cards. Its inverted secondary-slot register is visible at `0xFFFF`.

The MegaFlash subslot implements Konami SCC, Konami, linear 64 KiB, ASCII8,
and the updated 9-bit ASCII16 mapper behavior. The offset, DSK-remap,
mapper-lock, slot-expander, RAM-disable, PSG-mirror, recovery-protect, and
flash-write controls overlap the flash bus as on the cartridge. Mapper ports
`0xFC` through `0xFF` reset to `3,2,1,0` and combine with other memory mappers
through the machine's wired-AND bus behavior.

The 8 MiB M29W640GB model supplies manufacturer/device autoselect, CFI query
data, single, double, quadruple, and 32-byte buffered programming, bottom-boot
sector and chip erase, and recovery-block protection. Flash operations are
functionally immediate, but preserve the real command sequences and one-way
1-to-0 programming rule. A non-empty initial image up to 8 MiB seeds a
private flash-state file; shorter images, including the official
8,208,384-byte preflash, are padded with erased `0xFF` bytes. Guest changes
are written and synchronized to a same-directory
temporary file before atomic replacement; corruption is rejected, the source
dump remains untouched, and a failed flush blocks disconnection. Replacing the
configured initial image stages a private reseed that is atomically promoted
when overlay settings are saved; discarding the overlay leaves the prior state
untouched. Pending filenames include the selected source identity, allowing
startup to finish a promotion interrupted after configuration save without
attaching an unsaved reseed to the old configuration. Card-only edits preserve
the existing guest-programmed flash.

The MegaSD subslot uses an ASCII8 window into the final flash megabyte. Banks
`0x40` through `0x7F` expose the active-low card-select transfer window and
the two-socket selector. Each socket reuses `src/sdcard.c`, so read-only is
the default and complete-sector writes, explicit flush, failed-flush
retention, and safe ejection match SD Mapper V2.

The cartridge also contains an SCC-I with compatible and plus register maps,
and a YM2149-compatible PSG selected at ports `0x10`/`0x11`, optionally
mirrored at `0xA0`/`0xA1`. Both sound sources are generated independently
and mixed with the machine PSG. The Extensions setup selects the initial
flash and optional cards atomically; Media owns later card changes. CLI and
configuration use `megaflash_rom`, `megaflash_sd_a`, `megaflash_sd_b`, and
the shared SD image policy.

## MSX1 video

The TMS9918/TMS9929 path implements:

- Text, Graphics I, Graphics II, and Multicolour rendering;
- VRAM data and control ports;
- register and status behavior;
- vertical interrupts;
- sprite mode 1.

Sprite mode 1 supports 8×8 and 16×16 patterns, magnification, early clock,
priority, transparency, Y wrapping, four sprites per line, fifth-sprite
status, collision status, and the attribute-list terminator.

MSX1 rendering currently evaluates most state at frame boundaries.
Cycle-level raster changes remain future work.

## V9938 video

The V9938 implementation includes:

- CPU-visible control and status registers;
- programmable palette and indirect register access;
- 128 KiB VRAM with R14 paging and planar SCREEN 6/7 addressing;
- SCREEN 5 through SCREEN 8 bitmap rendering;
- 192/212-line output, display pages, vertical scrolling, palette
  transparency, and SCREEN 8 fixed colors;
- sprite mode 2 in SCREEN 4 through SCREEN 8;
- vertical and R19 horizontal interrupts;
- beam-positioned S#2 VR/HR status;
- progressive scanline rendering;
- border-inclusive 280×240 presentation with R#18 horizontal and vertical
  adjustment.

Sprite mode 2 supports per-line color attributes, EC/CC/IC behavior,
lower-index priority, eight sprites per line, ninth-sprite status,
transparent color, magnification, early clock, collision coordinates, and
the 192/212-line layouts.

All twelve bitmap commands are implemented: POINT, PSET, SRCH, LINE, LMMV,
LMMM, LMCM, LMMC, HMMV, HMMM, YMMM, and HMMC. Autonomous and CPU-transfer
commands advance with the emulated beam, including CE/TR handshakes,
preloaded transfers, logical operations, and CPU priority when both engines
request the same VRAM slot.

CPU VRAM traffic and command operations use measured screen-off, bitmap,
character, and text access schedules. Timed VRAM, palette, backdrop, page,
scroll, and display-enable changes preserve completed rows and affect later
ones. Pixel-level changes within the active scanline are not yet timed.

The timing work is cross-checked against the
[Yamaha V9938 Technical Data Book](https://map.grauw.nl/resources/video/yamaha_v9938.pdf),
the
[V9938 Programmer's Guide](https://ia800409.us.archive.org/25/items/tms9918_guide/V9938-programmers-guide%20insecure.pdf),
and openMSX's measured
[V9938 VRAM timings](https://openmsx.org/vdp-vram-timing/vdp-timing.html)
and [part II](https://openmsx.org/vdp-vram-timing/vdp-timing-2.html).

## Audio and RTC

The host-independent PSG implements AY-3-8910 and YM2149 variants with three
tone channels, the 17-bit noise generator, all envelope shapes, mixer
behavior, fixed and envelope volume curves, and volume-register DAC output.
It runs at MSX bus timing and produces filtered 44.1 kHz signed 16-bit mono
audio through SDL3.

PSG port A reads the selected joystick connector through register 14. Bits
0 through 5 are active-low Up, Down, Left, Right, trigger A, and trigger B.
Register 15 bit 6 selects connector A or B; bits 4 and 5 drive their
respective pin-8 lines. Joysticks retain the existing pin-8 gating, while an
MSX mouse uses pin 8 as its nibble strobe. Register 14 bit 6 is the low
international keyboard-layout signal and bit 7 is the cassette waveform
comparator, high when no tape is inserted. PSG port B also drives the Kana
LED.

The host-independent machine stores both joystick and mouse states separately
for both connectors. Each mouse reports signed X-high, X-low, Y-high, and
Y-low nibbles, exposes two active-low buttons, performs the alternate zero
cycle used for trackball detection, and resynchronizes after 1.5 ms without a
strobe. Host motion is accumulated, divided by two, direction-adjusted, and
clamped to signed 7-bit chunks following openMSX's Philips SBC3810 behavior.
The shared SCC/SCC-I core implements five programmable 32-byte waveforms,
period, volume, enable, deformation, compatible and plus register maps, and
cycle-derived 44.1 kHz output for Konami SCC and MegaFlashROM cartridges.
MegaFlashROM's cartridge PSG is mixed independently. MSX-MUSIC audio remains
future work.

MSX2 layouts include RP-5C01-compatible latch/data ports at `0xB4` and
`0xB5`, four 13-nibble register banks, hardware write masks, 12/24-hour
conversion, calendar and leap-year rollover, control/reset semantics, and
the 16,384 Hz seconds/minutes/days/years test modes. Time advances from
emulated Z80 cycles after its initial host-time seed, so headless and
unthrottled execution remains deterministic.

The battery-backed banks are stored per configured MSX2 machine in
`rtc/<machine-id>.cmos` beside `1983.conf`. The small format has a magic
number, version, payload length, last host timestamp, timer-running flag,
all 52 nibbles, and checksum. Loads require an exact size, valid checksum,
supported flags, masked BCD fields, and a valid calendar date. A missing
file is a normal first run; invalid state leaves the live host-seeded clock
untouched and reports a warning.

Dirty CMOS is flushed on machine transitions, when persistence is disabled,
and at normal shutdown. Saves write and synchronize a same-directory
temporary file before atomically replacing the old copy. Offline elapsed
host time is applied only when the saved timer was running. Hardware reset
preserves the battery-backed banks while resetting the latch and control
state. Tinker exposes the persistent-clock toggle and I/O warning state in
Advanced. `/dev/null` configurations disable host persistence for
deterministic frontend runs; component and firmware tests use injected
timestamps or explicitly pinned register values.

## Input

The complete 11-row international keyboard matrix is connected through PPI
ports `0xA9` and `0xAA`. It supports modifiers, function and editing keys,
numeric keypad, simultaneous-key rollover, host aliases, and focus-loss
cleanup.

SDL input is positional. The frontend reserves unmodified function keys;
Shift+F1 through Shift+F5 send the guest function keys, Shift+F7 sends
SELECT, and Shift+F8 sends STOP. Alternate national matrices and
model-specific electrical ghosting are not implemented.

Ctrl+V copies host clipboard text into a host-independent paste queue. ASCII
characters are translated with the international MSX Unicode mapping and
injected through the reference-counted matrix over successive frames. A
three-frame initial delay lets the physical Ctrl+V chord clear, every key is
held for two frames, and no implicit Return is appended. Pause suspends the
queue; reset, overlay entry, and focus loss cancel it safely. Chord
auto-repeat is filtered so holding Ctrl+V cannot restart the queue, and
repeated key-down events are ignored by the keyboard adapter so a held chord
cannot re-enter the guest matrix mid-paste.

The SDL3 controller adapter applies a 16,000-unit analogue dead zone and
normalizes opposing directions to neutral before passing a six-bit state to
the machine core. Relative SDL mouse input is captured by clicking the window
when the selected connector is configured as Mouse. Left/right host buttons
map to MSX buttons A/B; Ctrl+Enter, F9, reset, and focus loss release capture.
Both connector protocols, PSG selection, and pin handling remain independent
of SDL and are covered by deterministic tests.

## Frontend

The SDL3 frontend follows the shared 1984/1985 interface:

- resizable 640×480 guest output;
- a centred 192/212-line VDP image with authentic side and vertical borders;
- shortcut footer and MSX LED strip;
- F9 settings overlay;
- fullscreen and integer window scaling;
- screenshots, pause, reset, notifications, and headless execution;
- Power, Caps, Kana, drive, cassette, and IDE status surfaces.

Like 1984, the F9 overlay is drawn after the emulated canvas at a fixed 1.5×
debug-font scale. Resizing the guest image therefore does not enlarge the
menu text or row spacing; undersized windows reduce the scale only as far as
needed to keep the complete 640×480 menu layout available.

Notification mode `off` suppresses both on-screen pop-ups and routine startup
information on standard output. Warnings and errors remain on standard error;
explicit command output such as `--help`, `--version`, and `--dump-state`
remains visible.

The emulator is kept behind a narrow machine boundary: core hardware does
not depend on SDL and can run in component tests, headless tools, and future
frontends.

## Known major gaps

- Protected/flux floppy formats, format/write-track, and unusual geometries.
- Cassette recording and sampled audio input.
- CHS-only ATA edge cases and additional ATA commands.
- Alternate national keyboard layouts.
- MSX-MUSIC audio.
- Snapshots, debugger/disassembler, and animated capture.
- Within-scanline fetch timing and further compatibility refinement.

See [`ROADMAP.md`](ROADMAP.md) for sequencing and
[`BOOT_TARGETS.md`](BOOT_TARGETS.md) for the storage and firmware milestones.
