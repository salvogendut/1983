'use strict';

const assert = require('assert');
const { parseStartupMedia, filenameFromUrl } = require('./media-url.js');

const base = 'https://example.test/1983/';

let media = parseStartupMedia(
  '?theme=sapporo-dark&disk=media%2Fthisdisk.dsk&autorun=disc.bas',
  base
);
assert.deepStrictEqual(media, {
  disk: 'https://example.test/1983/media/thisdisk.dsk',
  cartridge: null,
  cartridge2: null,
  autorun: 'disc.bas',
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
  disk: null,
  cartridge: null,
  cartridge2: null,
  autorun: null,
});

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

console.log('server media URL tests passed');
