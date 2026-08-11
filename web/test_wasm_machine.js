'use strict';

const assert = require('assert');
const path = require('path');
const create1983 = require('./dist/1983.js');

async function main() {
  const module = await create1983({
    locateFile: file => path.join(__dirname, 'dist', file),
  });

  assert.strictEqual(module._poc_init(), 0, 'C-BIOS MSX1 must initialize');
  assert.strictEqual(module._poc_frame_hz(), 60);
  assert.strictEqual(module._poc_has_floppy(), 0);

  assert.strictEqual(
    module._poc_init_model(1, 0),
    0,
    'RainBIOS NMS 8250 must initialize'
  );
  assert.strictEqual(module._poc_frame_hz(), 50);
  assert.strictEqual(module._poc_has_floppy(), 1);
  for (let frame = 0; frame < 300; ++frame) module._poc_step();
  const pixelCount = module._poc_width() * module._poc_height();
  const pixelStart = module._poc_pixels() >>> 2;
  const colors = new Set(
    module.HEAPU32.subarray(pixelStart, pixelStart + pixelCount)
  );
  assert(colors.size > 1, 'RainBIOS must render a non-blank boot display');
  module._poc_audio_reset();

  module.FS.writeFile('/test.dsk', new Uint8Array(737280));
  assert.strictEqual(
    module.ccall('poc_load_disk', 'number', ['string'], ['/test.dsk']),
    0,
    'the NMS 8250 WD2793 must accept a disk image'
  );
  module._poc_eject_disk();

  const cartridge = new Uint8Array(0x2000);
  cartridge.fill(0xff);
  module.FS.writeFile('/test-slot-1.rom', cartridge);
  module.FS.writeFile('/test-slot-2.rom', cartridge);
  assert.strictEqual(
    module.ccall(
      'poc_load_cartridge_slot', 'number', ['number', 'string'],
      [0, '/test-slot-1.rom']
    ),
    0
  );
  assert.strictEqual(
    module.ccall(
      'poc_load_cartridge_slot', 'number', ['number', 'string'],
      [1, '/test-slot-2.rom']
    ),
    0
  );
  assert.strictEqual(module._poc_cartridge_loaded(0), 1);
  assert.strictEqual(module._poc_cartridge_loaded(1), 1);
  module._poc_eject_cartridge(1);
  assert.strictEqual(module._poc_cartridge_loaded(0), 1);
  assert.strictEqual(module._poc_cartridge_loaded(1), 0);

  assert.strictEqual(module._poc_set_input_device(0), 0);
  module._poc_joy(4, 1);
  assert.strictEqual(module._poc_joy_matrix() & 0x10, 0);
  module._poc_joy(4, 0);
  assert.strictEqual(module._poc_joy_matrix() & 0x3f, 0x3f);

  assert.strictEqual(module._poc_set_input_device(1), 0);
  module._poc_mouse_motion(12, -7);
  module._poc_mouse_button(0, 1);
  module._poc_step();
  module._poc_mouse_button(0, 0);
  module._poc_mouse_clear();
  assert.strictEqual(module._poc_set_input_device(0), 0);
  assert.strictEqual(module._poc_set_input_device(2), -1);

  assert.strictEqual(module._poc_init_model(0, 0), 0);
  assert.strictEqual(module._poc_frame_hz(), 60);
  assert.strictEqual(module._poc_has_floppy(), 0);
  assert.strictEqual(module._poc_cartridge_loaded(0), 0);
  assert.strictEqual(module._poc_cartridge_loaded(1), 0);

  console.log('WASM machine profile and peripheral smoke tests passed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
