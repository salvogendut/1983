# Javascript 1983

Build the browser edition with Emscripten and serve the publish directory:

    export PATH=/var/home/salvogendut/emsdk/upstream/emscripten:$PATH
    make -C web
    python3 -m http.server 8080 --directory web/dist

## What works

- The MSX core compiles to WebAssembly (z80, VDP, PSG, floppy, cassette,
  cartridge, keyboard, paste) with host-only modules stubbed. Boots an **MSX1
  (C-BIOS)** fully client-side — no host process, no frame streaming.
- **Byte-identical to the native emulator**: the VDP framebuffer hashes match
  exactly over 400 boot frames.
- Video rendered from the VDP framebuffer (256x192 MSX1 / 512x212 MSX2) scaled
  to a 4:3 canvas. Keyboard, PSG audio (schedule-ahead WebAudio), floppy DSK,
  cassette CAS, cartridge ROM and USB gamepad input.
- `poc_init_model(1, ...)` selects MSX2, which needs a real MSX2 BIOS
  (`MSX2.ROM`) in the emscripten virtual FS — not bundled.

## Server-hosted media

Media URLs are resolved relative to the page URL. A cartridge ROM, a floppy
disk and a cassette can be mounted at startup:

    http://localhost:8080/?cartridge=media/game.rom
    http://localhost:8080/?disk=media/thisdisk.dsk
    http://localhost:8080/?disk=media/thisdisk.dsk&autorun=load.bas

Parameter values containing spaces or other reserved characters must be URL
encoded. Media must be available over HTTP or HTTPS. Cross-origin servers must
permit the request with CORS headers.

The fetched image lives in the browser's in-memory filesystem. Guest writes
are not uploaded to the server.
