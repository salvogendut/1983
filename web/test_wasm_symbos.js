'use strict';

const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const create1983 = require('./dist/1983.js');

async function main() {
  const imagePath = process.argv[2];
  const screenshotPath = process.argv[3] || '/tmp/1983-wasm-symbos.ppm';
  const frameCount = Number(process.argv[4] || 5000);
  const controller = process.argv[5] || 'sunrise';
  if (!imagePath) {
    throw new Error(
      'usage: node test_wasm_symbos.js IMAGE [SCREENSHOT.ppm] [FRAMES] ' +
      '[sunrise|sdmapper]'
    );
  }
  if (controller !== 'sunrise' && controller !== 'sdmapper')
    throw new Error('controller must be sunrise or sdmapper');

  const image = fs.readFileSync(imagePath);
  const imageHash = crypto.createHash('sha256').update(image).digest('hex');

  const module = await create1983({
    locateFile: file => path.join(__dirname, 'dist', file),
  });
  assert.strictEqual(
    module._poc_init(), 0,
    'the default Omega MSX2 profile must initialize'
  );
  module.FS.writeFile('/symbos.img', image);
  if (controller === 'sunrise') {
    assert.strictEqual(module._poc_set_sunrise(1), 1);
    assert.strictEqual(
      module.ccall(
        'poc_mount_ide', 'number', ['string', 'number'],
        ['/symbos.img', 1]
      ),
      0
    );
  } else {
    assert.strictEqual(module._poc_set_sd_mapper(1), 1);
    assert.strictEqual(
      module.ccall(
        'poc_mount_sd_card', 'number', ['number', 'string', 'number'],
        [0, '/symbos.img', 1]
      ),
      0
    );
  }
  module._poc_reset();
  for (let frame = 0; frame < frameCount; ++frame) module._poc_step();

  const width = module._poc_width();
  const height = module._poc_height();
  const start = module._poc_pixels() >>> 2;
  const pixels = module.HEAPU32.subarray(start, start + width * height);
  const rgb = Buffer.alloc(width * height * 3);
  const colors = new Set();
  for (let source = 0, destination = 0; source < pixels.length; ++source) {
    const color = pixels[source];
    colors.add(color);
    rgb[destination++] = (color >>> 16) & 0xff;
    rgb[destination++] = (color >>> 8) & 0xff;
    rgb[destination++] = color & 0xff;
  }
  fs.writeFileSync(
    screenshotPath,
    Buffer.concat([Buffer.from(`P6\n${width} ${height}\n255\n`), rgb])
  );
  const pixel = (x, y) => pixels[y * width + x] & 0x00ffffff;
  const desktopColors = new Set();
  for (let y = Math.floor(height / 8); y < height - 24; ++y) {
    for (let x = Math.floor(width / 4); x < width - 16; ++x)
      desktopColors.add(pixel(x, y));
  }
  assert.strictEqual(width, 512, 'SymbOS must enter a 512-pixel MSX2 mode');
  assert.strictEqual(height, 212, 'SymbOS must enter a 212-line MSX2 mode');
  assert(
    colors.size >= 8 &&
    colors.has(0x009292ff) && colors.has(0x00000092) &&
    colors.has(0x00ffff92) && colors.has(0x00ffffff),
    `expected the SymbOS desktop palette (found ${colors.size} colors; ` +
    screenshotPath + ')'
  );
  assert(
    pixel(0, 0) === 0x00ffff92 || pixel(0, 0) === 0x000000ff,
    'expected SymbOS desktop or SymZilla chrome at the top-left corner'
  );
  assert(
    pixel(width - 1, 0) === 0x00000092 ||
    pixel(width - 1, 0) === 0x000000ff,
    'expected SymbOS desktop or SymZilla chrome at the top-right corner'
  );
  assert(
    desktopColors.size >= 6,
    `solid-colour SymbOS desktop indicates failed storage startup (` +
    `${desktopColors.size} background colours; ${screenshotPath})`
  );
  console.log(
    `WASM SymbOS ${controller} acceptance passed: ${frameCount} frames, ` +
    `${width}x${height}, ${colors.size} colors, SHA-256 ${imageHash}, ` +
    screenshotPath
  );
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
