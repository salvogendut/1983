'use strict';

const assert = require('assert');
const {
  parseStartupMedia,
  resolveStartupExtensions,
  filenameFromUrl,
} = require('./media-url.js');

const base = 'https://example.test/1983/';

let media = parseStartupMedia(
  '?theme=sapporo-dark&disk=media%2Fthisdisk.dsk&autorun=disc.bas',
  base
);
assert.deepStrictEqual(media, {
  machine: null,
  disk: 'https://example.test/1983/media/thisdisk.dsk',
  cartridge: null,
  cartridge2: null,
  sdA: null,
  sdB: null,
  extensions: null,
  autorun: 'disc.bas',
  sdMode: null,
});

media = parseStartupMedia(
  '?cartridge=https%3A%2F%2Fcdn.example.test%2Fgames%2FSonic.cpr',
  base
);
assert.strictEqual(media.disk, null);
assert.strictEqual(media.cartridge, 'https://cdn.example.test/games/Sonic.cpr');
assert.strictEqual(media.cartridge2, null);
assert.strictEqual(media.autorun, null);

media = parseStartupMedia(
  '?cartridge=media%2Fslot1.rom&cartridge2=media%2Fslot2.rom',
  base
);
assert.strictEqual(media.cartridge, 'https://example.test/1983/media/slot1.rom');
assert.strictEqual(media.cartridge2, 'https://example.test/1983/media/slot2.rom');

assert.strictEqual(
  filenameFromUrl('https://example.test/media/Bomb%20Jack.dsk', 'disk.dsk'),
  'Bomb Jack.dsk'
);
assert.deepStrictEqual(parseStartupMedia('?theme=default', base), {
  machine: null,
  disk: null,
  cartridge: null,
  cartridge2: null,
  sdA: null,
  sdB: null,
  extensions: null,
  autorun: null,
  sdMode: null,
});

media = parseStartupMedia(
  '?extensions=SDMAPPER%2Cunapi%2Cunapi' +
  '&sda=media%2Fsystem.img&sdb=media%2Fdata.img' +
  '&sdmode=readwrite&disk=media%2FGEOBENCH.DSK',
  base
);
assert.deepStrictEqual(media, {
  machine: null,
  disk: 'https://example.test/1983/media/GEOBENCH.DSK',
  cartridge: null,
  cartridge2: null,
  sdA: 'https://example.test/1983/media/system.img',
  sdB: 'https://example.test/1983/media/data.img',
  extensions: ['sdmapper', 'unapi'],
  autorun: null,
  sdMode: 'readwrite',
});
assert.deepStrictEqual(
  resolveStartupExtensions(media, { sdMapper: false, unapi: false }),
  { sdMapper: true, unapi: true }
);

media = parseStartupMedia('?sda=media%2Fsystem.img', base);
assert.deepStrictEqual(
  resolveStartupExtensions(media, { sdMapper: false, unapi: true }),
  { sdMapper: true, unapi: true },
  'an SD image implies SD Mapper without replacing stored UNAPI state'
);

media = parseStartupMedia('?extensions=unapi', base);
assert.deepStrictEqual(
  resolveStartupExtensions(media, { sdMapper: true, unapi: false }),
  { sdMapper: false, unapi: true },
  'an explicit extensions list overrides stored extension state'
);

assert.strictEqual(parseStartupMedia('?machine=msx1', base).machine, 0);
assert.strictEqual(parseStartupMedia('?machine=CBIOS', base).machine, 0);
assert.strictEqual(parseStartupMedia('?machine=nms8250', base).machine, 1);
assert.strictEqual(parseStartupMedia('?machine=MSX2', base).machine, 1);
assert.throws(
  () => parseStartupMedia('?machine=turbor', base),
  /machine must be msx1 or nms8250/
);

assert.throws(
  () => parseStartupMedia('?disk=file%3A%2F%2F%2Ftmp%2Fprivate.dsk', base),
  /HTTP or HTTPS/
);
assert.throws(
  () => parseStartupMedia('?autorun=disc.bas', base),
  /requires a disk/
);
assert.throws(
  () => parseStartupMedia('?disk=game.dsk&autorun=bad%22name', base),
  /unsupported characters/
);
assert.throws(
  () => parseStartupMedia('?extensions=sdmapper%2Cunknown', base),
  /unsupported extension/
);
assert.throws(
  () => parseStartupMedia('?sda=file%3A%2F%2F%2Ftmp%2Fprivate.img', base),
  /HTTP or HTTPS/
);
assert.throws(
  () => parseStartupMedia('?sdmode=readwrite', base),
  /requires an sda or sdb/
);
assert.throws(
  () => parseStartupMedia('?sda=system.img&sdmode=unsafe', base),
  /readonly or readwrite/
);

console.log('server media URL tests passed');
