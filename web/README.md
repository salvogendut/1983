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

## What works

- The MSX core compiles to WebAssembly (z80, VDP, PSG, floppy, cassette,
  cartridge, keyboard, mouse, paste) with host-only modules stubbed. It boots
  fully client-side — no host process and no frame streaming.
- Two redistributable machine profiles are bundled: **MSX1 (C-BIOS)** at 60 Hz
  and **Philips NMS 8250 (RainBIOS)** at 50 Hz. The latter includes its MSX2
  main ROM, Sub-ROM, WD2793 disk ROM, 128 KiB RAM and 128 KiB VRAM.
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
- **AUX expansion bay** with SD Mapper V2 and MSX TCP/IP UNAPI controls. The SD
  Mapper has fixed embedded firmware, 512 KiB mapper RAM, two user-selectable
  SD images and reserves cartridge II. UNAPI is port-mapped and leaves both
  cartridge slots available.

## AUX expansion bay

Press **AUX** to open the accessible side panel. Optional-device state and the
UNAPI relay endpoint are remembered in browser local storage.

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

## URL-configured hardware and media

Media URLs are resolved relative to the page URL. Two cartridge ROMs, a Drive A
floppy, and both SD Mapper cards can be mounted at startup:

    http://127.0.0.1:1983/?cartridge=media/game.rom
    http://127.0.0.1:1983/?cartridge=media/game.rom&cartridge2=media/tool.rom
    http://127.0.0.1:1983/?disk=media/thisdisk.dsk
    http://127.0.0.1:1983/?disk=media/thisdisk.dsk&autorun=load.bas

A startup disk automatically selects the NMS 8250 profile because the default
C-BIOS MSX1 profile has no floppy controller. Drive A is mounted before one
automatic reset, so a bootable disk starts without an `autorun` parameter.

The `extensions` parameter accepts `sdmapper`, `unapi`, or both as a
comma-separated list. An explicit list overrides stored extension toggles for
that page load without rewriting browser storage. `sda` and `sdb` load the two
SD Mapper cards and implicitly enable the mapper. URL-loaded SD images are
read-only unless `sdmode=readwrite` is supplied; writable changes remain local
and use the existing download-on-eject path.

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
