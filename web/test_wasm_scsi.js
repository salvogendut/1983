'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const create1983 = require('./dist/1983.js');

function screen2PromptPresent(module) {
  if (module._poc_width() !== 256 || module._poc_height() !== 192)
    return false;
  const width = module._poc_width();
  const start = module._poc_pixels() >>> 2;
  const pixels = module.HEAPU32.subarray(start, start + width * 192);
  const glyphs = [
    [
      '..###...', '.#...#..', '.#...#..', '.#####..',
      '.#...#..', '.#...#..', '.#...#..', '........',
    ],
    [
      '........', '..##....', '..##....', '........',
      '..##....', '..##....', '........', '........',
    ],
    [
      '.#......', '..#.....', '...#....', '....#...',
      '.....#..', '........', '........', '........',
    ],
    [
      '..#.....', '...#....', '....#...', '.....#..',
      '....#...', '...#....', '..#.....', '........',
    ],
  ];
  return glyphs.every((rows, cell) => rows.every((row, y) =>
    [...row].every((expected, x) => {
      const pixel = pixels[(8 + y) * width + cell * 8 + x] & 0x00ffffff;
      return (pixel === 0x00ffffff) === (expected === '#');
    })
  ));
}

async function main() {
  const romPath = process.argv[2];
  const imagePath = process.argv[3];
  const frameCount = Number(process.argv[4] || 1500);
  const unifiedRomPath = process.argv[5] || '';
  if (!romPath || !imagePath)
    throw new Error(
      'usage: node test_wasm_scsi.js ROM IMAGE [FRAMES] [UNIFIED-ROM]'
    );

  const output = [];
  const module = await create1983({
    locateFile: file => path.join(__dirname, 'dist', file),
    print: line => output.push(String(line)),
  });
  assert.strictEqual(module._poc_init(), 0);
  if (unifiedRomPath) {
    const unifiedRom = fs.readFileSync(unifiedRomPath);
    const unifiedPointer = module._malloc(unifiedRom.byteLength);
    assert.notStrictEqual(unifiedPointer, 0);
    try {
      module.HEAPU8.set(unifiedRom, unifiedPointer);
      assert.strictEqual(
        module._poc_install_omega_unified_rom(
          unifiedPointer, unifiedRom.byteLength
        ),
        0,
        'the supplied Omega unified ROM must install'
      );
    } finally {
      module._free(unifiedPointer);
    }
  }

  const rom = fs.readFileSync(romPath);
  const pointer = module._malloc(rom.byteLength);
  assert.notStrictEqual(pointer, 0);
  try {
    module.HEAPU8.set(rom, pointer);
    assert.strictEqual(
      module._poc_install_scsi_rom(pointer, rom.byteLength, 0), 0,
      'the supplied banked MSX SCSI controller ROM must install'
    );
  } finally {
    module._free(pointer);
  }
  assert.strictEqual(module._poc_set_scsi(1), 1);
  module.FS.writeFile('/scsi-acceptance.img', fs.readFileSync(imagePath));
  assert.strictEqual(
    module.ccall(
      'poc_mount_scsi', 'number', ['string', 'number'],
      ['/scsi-acceptance.img', 0]
    ),
    0,
    'the supplied raw SCSI disk image must mount read-only'
  );
  module._poc_reset();
  let activity = false;
  for (let frame = 0; frame < frameCount; ++frame) {
    module._poc_step();
    activity = Boolean(module._poc_scsi_activity()) || activity;
  }
  assert(activity, 'the guest must access the SCSI disk while booting');
  module._poc_dump_screen_text();
  const text = output.join('\n');
  assert(
    /A:\\?>/i.test(text) || screen2PromptPresent(module),
    'MSX-DOS2 must reach an A: prompt in text or Screen 2 output'
  );
  console.log(
    `WASM MSX SCSI boot acceptance passed: ${frameCount} frames, ` +
    `${rom.byteLength} byte ROM, ${fs.statSync(imagePath).size} byte image`
  );
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
