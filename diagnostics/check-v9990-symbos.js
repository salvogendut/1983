'use strict';

const assert = require('assert');
const fs = require('fs');

const path = process.argv[2];
if (!path)
  throw new Error('usage: node check-v9990-symbos.js SCREENSHOT.ppm');

const image = fs.readFileSync(path);
const match = image.subarray(0, 96).toString('ascii').match(
  /^P6\s+(\d+)\s+(\d+)\s+255\s/
);
assert(match, 'expected a binary PPM screenshot');
const width = Number(match[1]);
const height = Number(match[2]);
const dataOffset = match[0].length;
assert.strictEqual(
  image.length, dataOffset + width * height * 3,
  'truncated PPM screenshot'
);

const colorAt = (x, y) => {
  const offset = dataOffset + (y * width + x) * 3;
  return (image[offset] << 16) | (image[offset + 1] << 8) |
         image[offset + 2];
};
const colors = new Set();
let detailedRows = 0;
let cursorWhitePixels = 0;
for (let y = 0; y < height; ++y) {
  const rowColors = new Set();
  for (let x = 0; x < width; ++x) {
    const color = colorAt(x, y);
    colors.add(color);
    rowColors.add(color);
    /* SYMG9K leaves its white bitmap hardware cursor near screen centre. */
    if (x >= Math.floor(width * 0.49) && x < Math.ceil(width * 0.55) &&
        y >= Math.floor(height * 0.49) && y < Math.ceil(height * 0.58) &&
        (color & 0xf0f0f0) === 0xf0f0f0)
      ++cursorWhitePixels;
  }
  if (rowColors.size >= 6) ++detailedRows;
}

assert(width >= 384 && height >= 240,
       `unexpected PowerGraph output size ${width}x${height}`);
assert(colors.size >= 8,
       `SYMG9K did not render a SymbOS desktop (${colors.size} colors)`);
assert(detailedRows >= Math.floor(height / 8),
       `V9990 output lacks desktop detail (${detailedRows} detailed rows)`);
assert(cursorWhitePixels >= 64,
       `SYMG9K white hardware cursor is missing ` +
       `(${cursorWhitePixels} white cursor pixels)`);
console.log(
  `Native SYMG9K acceptance passed: ${width}x${height}, ` +
  `${colors.size} colors, ${detailedRows} detailed rows, ` +
  `${cursorWhitePixels} white cursor pixels, ${path}`
);
