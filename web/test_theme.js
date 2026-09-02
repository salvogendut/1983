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
  /class="trinitron-console" aria-label="Monitor controls"/.test(html),
  'the interactive monitor fascia must be exposed to assistive technology'
);
assert(
  /class="trinitron-standard-key"[^>]*data-video-standard-toggle/.test(html),
  'the Sony monitor fascia must expose the video-standard switch'
);
assert(
  /class="monitor-standard-key"[^>]*data-video-standard-toggle/.test(html),
  'the monitor badge must expose the video-standard switch in other themes'
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
  /html\[data-theme="sonyhb-f1xd"\] \.screen-badge\s*\{[^}]*display:\s*none;/s.test(sonyCss),
  'the Sony theme must replace the generic monitor controls with its fascia'
);
assert(
  /class="trinitron-sony"[^>]*>THONY<\/strong>/.test(html),
  'the monitor fascia must use the THONY parody mark'
);
assert(
  sonyCss.includes('content: "THONY     HB-F1XD     MSX2";'),
  'the machine body must use the THONY parody mark'
);

console.log('web theme tests passed');
