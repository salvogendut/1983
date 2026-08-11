const assert = require('assert');
const fs = require('fs');

const html = fs.readFileSync('index.html', 'utf8');
const baseCss = fs.readFileSync('styles.css', 'utf8');
const sonyCss = fs.readFileSync('theme-sonyhb-f1xd.css', 'utf8');

for (const className of [
  'trinitron-mark',
  'trinitron-console',
  'trinitron-switches',
  'trinitron-jacks',
  'trinitron-knobs',
  'trinitron-sony',
  'trinitron-power'
]) {
  assert(html.includes(`class="${className}"`), `missing ${className} markup`);
  assert(sonyCss.includes(`.${className}`), `missing ${className} theme styling`);
}

assert(
  baseCss.includes('.trinitron-mark, .trinitron-console { display: none; }'),
  'Trinitron decorations must stay hidden in non-Sony themes'
);
assert(
  /class="trinitron-console" aria-hidden="true"/.test(html),
  'decorative monitor controls must be hidden from assistive technology'
);
assert(
  /\.trinitron-console\s*\{[^}]*display:\s*grid;/s.test(sonyCss),
  'the Sony theme must enable the monitor control fascia'
);
assert(
  /\.trinitron-mark\s*\{[^}]*display:\s*flex;/s.test(sonyCss),
  'the Sony theme must enable the Trinitron mark'
);
assert(
  html.includes('class="trinitron-sony">THONY</strong>'),
  'the monitor fascia must use the THONY parody mark'
);
assert(
  sonyCss.includes('content: "THONY     HB-F1XD     MSX2";'),
  'the machine body must use the THONY parody mark'
);

console.log('web theme tests passed');
