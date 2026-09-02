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
assert.match(html, /MSX SCSI/);
assert.match(html, /NCR Z5380/);
assert.match(html, /id="scsiRomFile"/);
assert.match(html, /id="scsiDiskFile"/);
assert.match(html, /id="scsiTargetId"/);
assert.match(html, /id="scsiIoBase"/);
assert.match(
  html,
  /id="scsiIoBase"[\s\S]*option value="48" selected>30h-37h<\/option>/
);
assert.match(html, /PowerGraph V9990/);
assert.match(html, /id="powergraphToggle"/);
assert.match(html, /V9990 \/ 512 KB VRAM/);
assert.match(
  html,
  /name="powergraphOutput" value="auto" checked[\s\S]*name="powergraphOutput" value="msx"[\s\S]*name="powergraphOutput" value="v9990"/
);
assert.match(
  html,
  /id="ledA"[\s\S]*id="ledIde"[\s\S]*<span>IDE<\/span>[\s\S]*id="ledScsi"[\s\S]*<span>SCSI<\/span>/
);
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
assert.match(app, /m\._poc_set_scsi\(requested \? 1 : 0\)/);
assert.match(app, /m\._poc_set_scsi_io_base\(scsiIoBase\)/);
assert.match(app, /m\._poc_install_scsi_rom\(pointer, data\.byteLength, scsiTargetId\)/);
assert.match(app, /BertSCSI v2 \(30h-37h\)/);
assert.match(app, /BertSCSI v1 \(D0h-D7h\)/);
assert.match(app, /installBundledScsiController\(\)/);
assert.match(app, /scsiIoBaseEl\.disabled = scsiEnabled/);
assert.match(app, /m\._poc_eject_scsi_disk\(\)/);
assert.match(app, /m\._poc_set_powergraph_v9990\(requested \? 1 : 0\)/);
assert.match(app, /m\._poc_set_powergraph_video_source\(source\)/);
assert.match(app, /framebufferPtr = m\._poc_pixels\(\);[\s\S]*const w = m\._poc_width\(\)/);
assert.match(app, /ledIdeEl\.classList\.add\("on"\)/);
assert.match(
  html,
  /id="memoryExpansion" type="range"[^>]*min="0" max="8"[^>]*value="2"/
);
assert.match(html, /id="memoryValue"[^>]*>64 KiB<\/output>/);
assert.match(app, /m\._poc_set_ram_kb\(ramKb\)/);
assert.match(app, /RAM_STORAGE_PREFIX \+ currentModel/);
assert.strictEqual(
  [...html.matchAll(/data-video-standard-toggle/g)].length,
  2,
  'the Sony fascia and generic monitor must expose the video standard'
);
assert(
  app.indexOf('button.addEventListener("click", event =>') <
    app.indexOf('create1983({'),
  'video-standard clicks must be handled while the WASM module is loading'
);
assert.match(app, /m\._poc_set_video_standard\(requested\)/);
assert.match(app, /VIDEO_STANDARD_STORAGE_KEY/);
assert.match(app, /frameClock\.setRate\(m\._poc_frame_hz\(\)\)/);
assert.match(app, /locateFile\(path\)/);
assert.match(html, /app\.js\?v=@ASSET_REV@/);
assert.match(app, /window\.addEventListener\("paste"/);
assert.match(app, /event\.clipboardData\.getData\("text\/plain"\)/);
assert.match(app, /m\.ccall\("poc_paste_text"/);
assert.match(app, /event\.ctrlKey \|\| event\.metaKey/);
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
assert.match(app, /const slot = cartridgeExtensionSlot\("SD Mapper V2"\);/);
assert.match(
  html,
  /<input type="checkbox" id="pixelToggle">/,
  'Sharp pixels must be disabled by default'
);
const auxHtml = html.slice(
  html.indexOf('id="expansionPanel"'), html.indexOf('<main')
);
const auxRomChoosers = [...auxHtml.matchAll(
  /<input[^>]*type="file"[^>]*accept="[^"]*\.rom[^>]*>/gi
)];
assert.strictEqual(
  auxRomChoosers.length, 1,
  'only user-supplied MSX SCSI firmware may have an AUX ROM chooser'
);
assert.match(auxRomChoosers[0][0], /id="scsiRomFile"/);
assert(
  html.indexOf('unapi-relay-protocol.js') < html.indexOf('unapi-bridge.js') &&
  html.indexOf('unapi-bridge.js') < html.indexOf('1983.js'),
  'UNAPI browser glue must load before the Emscripten module'
);

console.log('web UI contract tests passed');
