# Javascript 1983

Build the browser edition with Emscripten and serve the publish directory:

    export PATH=/var/home/salvogendut/emsdk/upstream/emscripten:$PATH
    make -C web
    python3 -m http.server 8080 --directory web/dist

Run the JavaScript unit tests and the built-WASM machine/peripheral smoke test
with:

    make -C web test
    make -C web check-wasm

Open `http://localhost:8080/` in a modern browser. The interface defaults to
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
- Cartridge II is disabled whenever the expansion reservation is active,
  matching the native emulator's cartridge-slot ownership rule.

## Server-hosted media

Media URLs are resolved relative to the page URL. Two cartridge ROMs, a floppy
disk and a cassette can be mounted at startup:

    http://localhost:8080/?cartridge=media/game.rom
    http://localhost:8080/?cartridge=media/game.rom&cartridge2=media/tool.rom
    http://localhost:8080/?disk=media/thisdisk.dsk
    http://localhost:8080/?disk=media/thisdisk.dsk&autorun=load.bas

A startup disk automatically selects the NMS 8250 profile because the default
C-BIOS MSX1 profile has no floppy controller.

Parameter values containing spaces or other reserved characters must be URL
encoded. Media must be available over HTTP or HTTPS. Cross-origin servers must
permit the request with CORS headers.

The fetched image lives in the browser's in-memory filesystem. Guest writes
are not uploaded to the server.
