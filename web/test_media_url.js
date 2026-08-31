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
  unifiedRom: null,
  disk: 'https://example.test/1983/media/thisdisk.dsk',
  cartridge: null,
  cartridge2: null,
  ide: null,
  scsiRom: null,
  scsi: null,
  sdA: null,
  sdB: null,
  extensions: null,
  autorun: 'disc.bas',
  sdMode: null,
  ideMode: null,
  scsiMode: null,
  scsiTargetId: null,
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
  unifiedRom: null,
  disk: null,
  cartridge: null,
  cartridge2: null,
  ide: null,
  scsiRom: null,
  scsi: null,
  sdA: null,
  sdB: null,
  extensions: null,
  autorun: null,
  sdMode: null,
  ideMode: null,
  scsiMode: null,
  scsiTargetId: null,
});

media = parseStartupMedia(
  '?extensions=SDMAPPER%2Cunapi%2Cunapi' +
  '&sda=media%2Fsystem.img&sdb=media%2Fdata.img' +
  '&sdmode=readwrite&disk=media%2FGEOBENCH.DSK',
  base
);
assert.deepStrictEqual(media, {
  machine: null,
  unifiedRom: null,
  disk: 'https://example.test/1983/media/GEOBENCH.DSK',
  cartridge: null,
  cartridge2: null,
  ide: null,
  scsiRom: null,
  scsi: null,
  sdA: 'https://example.test/1983/media/system.img',
  sdB: 'https://example.test/1983/media/data.img',
  extensions: ['sdmapper', 'unapi'],
  autorun: null,
  sdMode: 'readwrite',
  ideMode: null,
  scsiMode: null,
  scsiTargetId: null,
});
assert.deepStrictEqual(
  resolveStartupExtensions(media, {
    sunrise: false, scsi: false, sdMapper: false, powergraph: false, unapi: false,
  }),
  { sunrise: false, scsi: false, sdMapper: true, powergraph: false, unapi: true }
);

media = parseStartupMedia('?sda=media%2Fsystem.img', base);
assert.deepStrictEqual(
  resolveStartupExtensions(media, {
    sunrise: false, scsi: false, sdMapper: false, powergraph: false, unapi: true,
  }),
  { sunrise: false, scsi: false, sdMapper: true, powergraph: false, unapi: true },
  'an SD image implies SD Mapper without replacing stored UNAPI state'
);

media = parseStartupMedia('?extensions=unapi', base);
assert.deepStrictEqual(
  resolveStartupExtensions(media, {
    sunrise: true, scsi: false, sdMapper: true, powergraph: true, unapi: false,
  }),
  { sunrise: false, scsi: false, sdMapper: false, powergraph: false, unapi: true },
  'an explicit extensions list overrides stored extension state'
);

media = parseStartupMedia(
  '?ide=media%2Fsymbos.img&idemode=readwrite&extensions=sdmapper',
  base
);
assert.strictEqual(media.ide, 'https://example.test/1983/media/symbos.img');
assert.strictEqual(media.ideMode, 'readwrite');
assert.deepStrictEqual(
  resolveStartupExtensions(media, {
    sunrise: false, scsi: false, sdMapper: false, powergraph: false, unapi: true,
  }),
  { sunrise: true, scsi: false, sdMapper: true, powergraph: false, unapi: false },
  'an IDE image implies Sunrise alongside explicitly requested extensions'
);

media = parseStartupMedia('?extensions=powergraph%2Cunapi', base);
assert.deepStrictEqual(
  resolveStartupExtensions(media, {
    sunrise: false, scsi: false, sdMapper: false, powergraph: false, unapi: false,
  }),
  { sunrise: false, scsi: false, sdMapper: false, powergraph: true, unapi: true },
  'PowerGraph can be selected by URL without consuming the UNAPI port device'
);

assert.throws(
  () => resolveStartupExtensions(
    parseStartupMedia(
      '?extensions=sunrise%2Csdmapper%2Cpowergraph', base
    )
  ),
  /only two cartridge extensions/
);

assert.strictEqual(parseStartupMedia('?machine=msx1', base).machine, 0);
assert.strictEqual(parseStartupMedia('?machine=CBIOS', base).machine, 0);
assert.strictEqual(parseStartupMedia('?machine=nms8250', base).machine, 1);
assert.strictEqual(parseStartupMedia('?machine=philips', base).machine, 1);
assert.strictEqual(parseStartupMedia('?machine=omega', base).machine, 2);
assert.strictEqual(parseStartupMedia('?machine=omega-msx2', base).machine, 2);
assert.strictEqual(parseStartupMedia('?machine=MSX2', base).machine, 2);
assert.strictEqual(
  parseStartupMedia('?unifiedrom=media%2Fomega.rom', base).unifiedRom,
  'https://example.test/1983/media/omega.rom'
);
assert.throws(
  () => parseStartupMedia('?machine=turbor', base),
  /machine must be msx1, omega-msx2, or nms8250/
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

media = parseStartupMedia(
  '?extensions=scsi&scsirom=media%2FSCSI.ROM.BIN' +
  '&scsi=media%2FMSXDOS2.img&scsimode=readwrite&scsiid=4',
  base
);
assert.strictEqual(media.scsiRom, 'https://example.test/1983/media/SCSI.ROM.BIN');
assert.strictEqual(media.scsi, 'https://example.test/1983/media/MSXDOS2.img');
assert.strictEqual(media.scsiMode, 'readwrite');
assert.strictEqual(media.scsiTargetId, 4);
assert.deepStrictEqual(
  resolveStartupExtensions(media, {
    sunrise: false, scsi: false, sdMapper: false, powergraph: false, unapi: false,
  }),
  { sunrise: false, scsi: true, sdMapper: false, powergraph: false, unapi: false }
);
assert.throws(
  () => parseStartupMedia('?scsi=media%2Fdisk.img', base),
  /requires a scsirom URL/
);
assert.throws(
  () => parseStartupMedia('?scsirom=media%2Fcontroller.rom&scsiid=7', base),
  /integer from 0 through 6/
);
assert.throws(
  () => parseStartupMedia('?scsimode=readwrite', base),
  /requires a scsi URL/
);
assert.throws(
  () => parseStartupMedia('?idemode=readwrite', base),
  /requires an ide/
);
assert.throws(
  () => parseStartupMedia('?ide=system.img&idemode=unsafe', base),
  /readonly or readwrite/
);

console.log('server media URL tests passed');
