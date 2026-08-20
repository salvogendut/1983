'use strict';

const assert = require('assert');
const {
  INPUT_JOYSTICK,
  INPUT_MOUSE,
  normalizeInputDevice,
  createPeripheralState,
} = require('./hardware-ui.js');

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
  state.setCartridgeExtensions(['SD Mapper V2', 'SD Mapper V2']),
  ['SD Mapper V2']
);
assert.strictEqual(state.cartridgeSlotAvailable(0), false);
assert.strictEqual(state.cartridgeSlotAvailable(1), true);
assert.strictEqual(state.cartridgeSlotOwner(0), 'SD Mapper V2');

state.setCartridgeExtensions([]);
assert.strictEqual(state.cartridgeSlotAvailable(0), true);
assert.throws(() => state.cartridgeSlotAvailable(2), /unsupported/);

console.log('web hardware UI tests passed');
