'use strict';

const assert = require('assert');
const {
  INPUT_JOYSTICK,
  INPUT_MOUSE,
  OMEGA_UNIFIED_ROM_SIZE,
  RAM_SIZES_KB,
  ramSizesForModel,
  normalizeRamKb,
  defaultRamKb,
  formatRamKb,
  validateOmegaUnifiedRomSize,
  normalizeInputDevice,
  createPeripheralState,
} = require('./hardware-ui.js');

assert.deepStrictEqual(
  RAM_SIZES_KB,
  [16, 32, 64, 128, 256, 512, 1024, 2048, 4096]
);
assert.deepStrictEqual(ramSizesForModel(0), RAM_SIZES_KB);
assert.deepStrictEqual(
  ramSizesForModel(1),
  [64, 128, 256, 512, 1024, 2048, 4096]
);
assert.deepStrictEqual(
  ramSizesForModel(2),
  [64, 128, 256, 512, 1024, 2048, 4096]
);
assert.strictEqual(normalizeRamKb(0, 16), 16);
assert.strictEqual(normalizeRamKb(1, 16), 64);
assert.strictEqual(normalizeRamKb(1, 1000), 512);
assert.strictEqual(normalizeRamKb(1, 4096), 4096);
assert.strictEqual(normalizeRamKb(2, 16), 64);
assert.strictEqual(normalizeRamKb(2, 4096), 4096);
assert.strictEqual(defaultRamKb(0), 64);
assert.strictEqual(defaultRamKb(1), 128);
assert.strictEqual(defaultRamKb(2), 128);
assert.strictEqual(formatRamKb(512), '512 KiB');
assert.strictEqual(formatRamKb(1024), '1 MiB');
assert.strictEqual(formatRamKb(4096), '4 MiB');
assert.strictEqual(OMEGA_UNIFIED_ROM_SIZE, 524288);
assert.strictEqual(validateOmegaUnifiedRomSize(524288), 524288);
assert.throws(
  () => validateOmegaUnifiedRomSize(524287),
  /exactly 512 KiB/
);
assert.throws(
  () => validateOmegaUnifiedRomSize(524289),
  /exactly 512 KiB/
);
assert.throws(() => ramSizesForModel(3), /unsupported/);

const state = createPeripheralState();
assert.strictEqual(state.getInputDevice(), INPUT_JOYSTICK);
assert.strictEqual(state.cartridgeSlotAvailable(0), true);
assert.strictEqual(state.cartridgeSlotAvailable(1), true);

assert.strictEqual(state.setInputDevice(INPUT_MOUSE), INPUT_MOUSE);
assert.strictEqual(state.getInputDevice(), INPUT_MOUSE);
assert.throws(() => normalizeInputDevice('lightpen'), /unsupported/);

assert.deepStrictEqual(
  state.setPortExtensions(['MSX TCP/IP UNAPI', 'MSX TCP/IP UNAPI']),
  ['MSX TCP/IP UNAPI']
);
assert.strictEqual(state.cartridgeSlotAvailable(0), true);
assert.strictEqual(state.cartridgeSlotAvailable(1), true);

assert.deepStrictEqual(
  state.setCartridgeExtensions(['Sunrise IDE', 'SD Mapper V2', 'SD Mapper V2']),
  ['Sunrise IDE', 'SD Mapper V2']
);
assert.strictEqual(state.cartridgeSlotAvailable(0), false);
assert.strictEqual(state.cartridgeSlotOwner(0), 'Sunrise IDE');
assert.strictEqual(state.cartridgeSlotOwner(1), 'SD Mapper V2');
assert.strictEqual(state.cartridgeSlotAvailable(1), false);

state.setCartridgeExtensions([]);
assert.strictEqual(state.cartridgeSlotAvailable(0), true);
assert.throws(() => state.cartridgeSlotAvailable(2), /unsupported/);

console.log('web hardware UI tests passed');
