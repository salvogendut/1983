# Javascript 1983

Install the relay dependency once, then build and launch the browser edition:

    export PATH=/var/home/salvogendut/emsdk/upstream/emscripten:$PATH
    npm --prefix web/relay ci
    make -C web serve

Open `http://127.0.0.1:1983/`. The launcher serves the static browser application
and its restricted UNAPI WebSocket relay on the same origin, so the default
`/unapi` endpoint works without further configuration. Emulation and media
handling still execute entirely in the browser; the companion relay exists only
because browsers cannot open the raw TCP and UDP sockets used by MSX software.
Choose another same-origin port when 1983 is already occupied with, for example,
`make -C web serve UNAPI_PORT=19830`.

Run the JavaScript unit tests, relay integration tests, and the built-WASM
machine/peripheral smoke test with:

    make -C web test
    make -C web test-relay
    make -C web check-wasm

The interface defaults to
the **SONYHB-F1XD** theme: a charcoal, red-accented machine and compact
Trinitron/PVM-inspired monitor with a recessed tube and period control fascia.
The treatment is CSS-native and inspired by 1980s Japanese electronics rather
than reproducing one specific monitor model. Its full MSX keyboard is collapsed
by default; use **Show keyboard** to reveal it.

The theme picker also retains Retro CRT, Sapporo and Sapporo Dark. A theme can
be selected directly with a case-insensitive query parameter:

    http://localhost:8080/?theme=SONYHB-F1XD
    http://localhost:8080/?theme=retro-crt

Both the red **1983** masthead wordmark and the 1983 monitor-side logo link to
the project repository in a new browser tab.

## What works

- The MSX core compiles to WebAssembly (z80, VDP, PSG, floppy, cassette,
  cartridge, keyboard, mouse, paste) with host-only modules stubbed. It boots
  fully client-side — no host process and no frame streaming.
- Three redistributable machine profiles are bundled. **Omega MSX2
  (RainBIOS)** is the 50 Hz default and boots the same 512 KiB unified Omega
  EEPROM image as the native application, selecting its lower JP1 bank. It
  includes the MSX2 main ROM, Sub-ROM, generic WD2793 disk ROM, 128 KiB RAM,
  and 128 KiB VRAM. **MSX1 (C-BIOS)** at 60 Hz and **Philips NMS 8250
  (RainBIOS)** at 50 Hz remain selectable.
- The **ROM Upload** button beside the machine selector accepts a user-provided
  512 KiB Omega unified ROM. A valid image selects the Omega MSX2 profile when
  necessary and immediately reboots into its lower 256 KiB JP1 bank. The ROM
  remains only in browser memory for the current page session; its filename is
  shown below the selector, and later Omega resets continue using it.
- The **System RAM** slider below the machine selector exposes the same memory
  sizes as the native emulator. MSX1 supports 16, 32, 64, 128, 256 and 512 KiB,
  plus 1, 2 and 4 MiB; both MSX2 profiles support 64 KiB through 4 MiB. Each
  machine's selection is remembered independently. Changing it resets the
  guest while retaining mounted media and configured extensions.
- **Byte-identical to the native emulator**: the VDP framebuffer hashes match
  exactly over 400 boot frames.
- Video rendered from the VDP framebuffer (256x192 MSX1 / 512x212 MSX2) scaled
  to a 4:3 canvas. Keyboard, PSG audio (schedule-ahead WebAudio), floppy DSK,
  cassette CAS, two cartridge ROM slots, USB gamepad and MSX mouse input.
- Emulation pacing follows the core's reported 50/60 Hz rate. The Web Audio
  queue uses only samples actually produced by the machine, with bounded
  catch-up after browser stalls and no silence-padding between buffers.
- Physical keyboard input and the collapsible on-screen MSX keyboard, including
  F1-F5, SELECT, STOP, GRAPH, CODE, cursor keys and numeric keypad.
- Selecting **Mouse** captures relative pointer motion when the display is
  clicked. Press **Ctrl+Enter** (or the browser's pointer-lock escape key) to
  release it.
- **AUX expansion bay** with Sunrise IDE, SD Mapper V2 and MSX TCP/IP UNAPI
  controls. Sunrise and SD Mapper have fixed embedded Nextor firmware and safe
  read-only/read-write image handling. They reserve cartridge slots in device
  order; UNAPI is port-mapped and leaves both slots available.

## AUX expansion bay

Press **AUX** to open the accessible side panel. Optional-device state and the
UNAPI relay endpoint are remembered in browser local storage.

**Sunrise IDE** embeds the unmodified official Nextor 2.1.1 Sunrise kernel.
Enable it, choose read-only or read/write access and load a raw IDE image. A
writable image remains in the browser's in-memory filesystem and is flushed and
downloaded on safe ejection. IDE transfers illuminate both the AUX device lamp
and the dedicated receiver-top IDE activity LED. On the Philips profile,
connecting Sunrise suppresses the separate internal disk ROM to provide an
external-controller boot lane. The Omega unified image retains its physical
slot contents. The bundled firmware's notice is available as
[nextor-license.txt](dist/nextor-license.txt).

**SD Mapper V2** embeds `SDM V2 Nextor2.1.1.rom`; there is intentionally no ROM
chooser. Enable the device, select read-only or read/write access, then load an
image into SD A or SD B. Read/write images live in the browser's in-memory
filesystem while mounted. Safe ejection flushes the image and downloads its
updated contents. Disabling the mapper also safely ejects mounted images.

**MSX TCP/IP UNAPI** implements the same 28h/29h host bridge used by the native
openMSXnet-compatible device. The guest must load `UNAPINET.COM`. Web browsers
cannot open arbitrary TCP or UDP sockets, so the browser bridge connects to a
restricted WebSocket relay. See [UNAPI-WASM.md](UNAPI-WASM.md) for setup and
security details.

Sunrise occupies cartridge I. If both cartridge extensions are enabled, SD
Mapper moves to cartridge II; if Sunrise is disabled, SD Mapper moves back to
cartridge I. The normal cartridge controls display the corresponding reserved
slots.

## URL-configured hardware and media

Media URLs are resolved relative to the page URL. Two cartridge ROMs, a Drive A
floppy, a Sunrise IDE image and both SD Mapper cards can be mounted at startup:

    http://127.0.0.1:1983/?machine=msx1
    http://127.0.0.1:1983/?machine=omega-msx2
    http://127.0.0.1:1983/?machine=nms8250
    http://127.0.0.1:1983/?cartridge=media/game.rom
    http://127.0.0.1:1983/?cartridge=media/game.rom&cartridge2=media/tool.rom
    http://127.0.0.1:1983/?disk=media/thisdisk.dsk
    http://127.0.0.1:1983/?disk=media/thisdisk.dsk&autorun=load.bas
    http://127.0.0.1:1983/?machine=omega-msx2&ide=media/symbos.img

The `machine` parameter accepts `omega-msx2` for the default Omega RainBIOS
profile, `msx1` for C-BIOS, or `nms8250` for Philips NMS 8250 RainBIOS. The
aliases `omega` and `msx2`, `cbios`, and `philips` are also accepted for those
profiles respectively. The requested machine is selected before extensions
and media are applied and affects only that page load.

With the display focused, unshifted **F3** flips the bundled or uploaded Omega
unified image between its lower and upper 256 KiB JP1 banks and resets the guest.
**Shift+F3** continues to send the ordinary MSX F3 key. On the C-BIOS and
Philips profiles, F3 reports that no unified ROM is active.

Without an explicit `machine`, a startup disk uses the default Omega MSX2 and
its WD2793 controller. Drive A is mounted before one automatic reset, so a
bootable disk starts without an `autorun` parameter. An explicit `machine=msx1`
combined with a `disk` reports that the selected machine has no floppy
controller instead of silently changing profiles.

The `extensions` parameter accepts `sunrise`, `sdmapper`, and `unapi` as a
comma-separated list. An explicit list overrides stored extension toggles for
that page load without rewriting browser storage. `ide` loads an IDE master
image and implicitly enables Sunrise; `sda` and `sdb` load the two SD Mapper
cards and implicitly enable that mapper. URL-loaded images are read-only unless
`idemode=readwrite` or `sdmode=readwrite` is supplied. Writable changes remain
local and use the download-on-eject path.

For example, a Sunrise-configured SymbOS image can be launched with:

    http://127.0.0.1:1983/?machine=omega-msx2&ide=media/MSXSYMBOS.img&idemode=readwrite

The reference SymZilla installation writes startup state and must be mounted
read/write. Browser-side changes remain in memory and are offered as a download
when the image is safely ejected; the server-hosted source file is not changed.

An SD Mapper copy must have its SymbOS storage driver configured for native
SD Mapper V2 access, physical slot I, subslot 0, card A and partition 1:

    http://127.0.0.1:1983/?machine=omega-msx2&sda=media/MSXSYMBOS-SD.img&sdmode=readwrite

For example, this enables UNAPI, mounts an SD image in SD A, implicitly enables
SD Mapper V2, mounts the GeoBench floppy, and resets with everything present:

    http://127.0.0.1:1983/?extensions=unapi&sda=media/GBMSX.IMG&disk=media/GEOBENCH.DSK

Use `extensions=sdmapper,unapi` when enabling both without an SD image. The
existing `unapiRelay` parameter can still select a non-default relay endpoint.
If SD A contains a bootable operating system it may take priority over Drive A;
an empty or non-bootable SD card lets Nextor continue to the floppy.

Parameter values containing spaces or other reserved characters must be URL
encoded. Media must be available over HTTP or HTTPS. Cross-origin servers must
permit the request with CORS headers.

Fetched images live in the browser's in-memory filesystem. Guest writes are not
uploaded to the server.

For a repeatable local WASM acceptance run with a user-supplied SymbOS image,
the harness mounts its private in-memory copy read/write and requires the
SymbOS desktop palette and window geometry:

    make -C web check-wasm-symbos SYMBOS_IMAGE=/path/to/symbos.img
    make -C web check-wasm-symbos SYMBOS_IMAGE=/path/to/symbos-sd.img \
      SYMBOS_CONTROLLER=sdmapper
