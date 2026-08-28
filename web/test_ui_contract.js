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
assert.match(html, /Sunrise IDE/);
assert.match(html, /id="ledA"[\s\S]*id="ledIde"[\s\S]*<span>IDE<\/span>/);
assert.match(html, /Embedded Nextor 2\.1\.1/);
assert.match(html, /href="nextor-license\.txt"/);
assert.match(html, /MSX TCP\/IP UNAPI/);
assert.match(html, /Port-mapped \/ no cartridge slot/);
assert.match(html, /id="unapiCertificate"[^>]*>Trust certificate<\/button>/);
assert.match(html, /Approve its certificate, then return here/);
assert.match(
  html,
  /For details see <a href="https:\/\/github\.com\/salvogendut\/ws-unapi-relay" target="_blank" rel="noopener noreferrer">ws-unapi-relay<\/a>/
);
assert.match(app, /relayHealthEndpoint\(unapiEndpoint\)/);
assert.match(app, /window\.open\(unapiCertificateUrl, "_blank", "noopener,noreferrer"\)/);
assert.match(app, /m\._poc_set_sunrise\(requested \? 1 : 0\)/);
assert.match(app, /ledIdeEl\.classList\.add\("on"\)/);
assert.match(
  html,
  /id="memoryExpansion" type="range"[^>]*min="0" max="8"[^>]*value="2"/
);
assert.match(html, /id="memoryValue"[^>]*>64 KiB<\/output>/);
assert.match(app, /m\._poc_set_ram_kb\(ramKb\)/);
assert.match(app, /RAM_STORAGE_PREFIX \+ currentModel/);
assert.match(
  html,
  /id="unifiedRomLoad"[\s\S]*id="unifiedRomFile"[\s\S]*id="unifiedRomName"/
);
assert.match(html, /Upload a 512 KiB Omega unified ROM/);
assert.match(app, /validateOmegaUnifiedRomSize\(data\.byteLength\)/);
assert.match(app, /m\._poc_install_omega_unified_rom\(pointer, data\.byteLength\)/);
const repositoryLinks = [...html.matchAll(
  /<a[^>]*href="https:\/\/github\.com\/salvogendut\/1983"[^>]*>/g
)];
assert.strictEqual(repositoryLinks.length, 2);
for (const [link] of repositoryLinks) {
  assert.match(link, /target="_blank"/);
  assert.match(link, /rel="noopener noreferrer"/);
}
assert.match(html, /class="brand-repo-link"/);
assert.match(html, /class="screen-brand"/);
assert.match(app, /const slot = sunriseEnabled \? 1 : 0;/);
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
