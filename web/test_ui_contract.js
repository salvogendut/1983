'use strict';

const assert = require('assert');
const fs = require('fs');

const html = fs.readFileSync('index.html', 'utf8');
const app = fs.readFileSync('app.js', 'utf8');
const ids = new Set([...html.matchAll(/\sid="([^"]+)"/g)].map(match => match[1]));

for (const match of app.matchAll(/\$\("([^"]+)"\)/g)) {
  assert(ids.has(match[1]), `app.js expects missing #${match[1]}`);
}

assert.match(html, /SD Mapper V2/);
assert.match(html, /Embedded Nextor 2\.1\.1/);
assert.match(html, /MSX TCP\/IP UNAPI/);
assert.match(html, /Port-mapped \/ no cartridge slot/);
assert.match(html, /id="unapiCertificate"[^>]*>Trust certificate<\/button>/);
assert.match(html, /Approve its certificate, then return here/);
assert.match(app, /relayHealthEndpoint\(unapiEndpoint\)/);
assert.match(app, /window\.open\(unapiCertificateUrl, "_blank", "noopener,noreferrer"\)/);
assert.match(
  html,
  /<input type="checkbox" id="pixelToggle">/,
  'Sharp pixels must be disabled by default'
);
assert.doesNotMatch(
  html.slice(html.indexOf('id="expansionPanel"'), html.indexOf('<main')),
  /accept="[^"]*\.rom/i,
  'the AUX panel must not expose extension firmware choosers'
);
assert(
  html.indexOf('unapi-relay-protocol.js') < html.indexOf('unapi-bridge.js') &&
  html.indexOf('unapi-bridge.js') < html.indexOf('1983.js'),
  'UNAPI browser glue must load before the Emscripten module'
);

console.log('web UI contract tests passed');
