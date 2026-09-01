'use strict';

const assert = require('assert');
const path = require('path');
const create1983 = require('./dist/1983.js');

async function main() {
  const module = await create1983({
    locateFile: file => path.join(__dirname, 'dist', file),
  });

  assert.strictEqual(
    module._poc_init(), 0,
    'the default RainBIOS Omega MSX2 must initialize'
  );
  assert.strictEqual(module._poc_frame_hz(), 50);
  assert.strictEqual(module._poc_has_floppy(), 1);
  assert.strictEqual(module._poc_omega_unified_bank(), 0);
  assert.strictEqual(module._poc_flip_omega_unified_bank(), 1);
  assert.strictEqual(module._poc_omega_unified_bank(), 1);
  assert.strictEqual(module._poc_flip_omega_unified_bank(), 0);
  assert.strictEqual(module._poc_omega_unified_bank(), 0);

  const unifiedRom = module.FS.readFile('roms/rainbios_omega.rom');
  assert.strictEqual(unifiedRom.byteLength, 512 * 1024);
  const unifiedRomPointer = module._malloc(unifiedRom.byteLength);
  assert.notStrictEqual(unifiedRomPointer, 0);
  try {
    module.HEAPU8.set(unifiedRom, unifiedRomPointer);
    assert.strictEqual(
      module._poc_install_omega_unified_rom(
        unifiedRomPointer, unifiedRom.byteLength - 1
      ),
      -1,
      'a unified ROM shorter than 512 KiB must be rejected'
    );
    assert.strictEqual(
      module._poc_install_omega_unified_rom(
        unifiedRomPointer, unifiedRom.byteLength
      ),
      0,
      'a browser-supplied 512 KiB unified ROM must be installed'
    );
  } finally {
    module._free(unifiedRomPointer);
  }
  assert.strictEqual(module._poc_omega_unified_bank(), 0);
  assert.strictEqual(module._poc_flip_omega_unified_bank(), 1);
  assert.strictEqual(
    module._poc_init_model(2, 0),
    0,
    'the uploaded unified ROM must survive an Omega machine reboot'
  );
  assert.strictEqual(module._poc_omega_unified_bank(), 1);
  assert.strictEqual(module._poc_flip_omega_unified_bank(), 0);
  assert.strictEqual(module._poc_ram_kb(), 128);
  assert.strictEqual(module._poc_set_ram_kb(16), -1);
  assert.strictEqual(module._poc_ram_kb(), 128);
  assert.strictEqual(module._poc_set_ram_kb(4096), 4096);
  assert.strictEqual(module._poc_ram_kb(), 4096);
  assert.strictEqual(module._poc_set_ram_kb(1000), -1);
  assert.strictEqual(module._poc_ram_kb(), 4096);
  assert.strictEqual(module._poc_set_ram_kb(128), 128);
  for (let frame = 0; frame < 300; ++frame) module._poc_step();
  let pixelCount = module._poc_width() * module._poc_height();
  let pixelStart = module._poc_pixels() >>> 2;
  let colors = new Set(
    module.HEAPU32.subarray(pixelStart, pixelStart + pixelCount)
  );
  assert(colors.size > 1, 'Omega RainBIOS must render a non-blank boot display');

  module.FS.writeFile('/omega-test.dsk', new Uint8Array(737280));
  assert.strictEqual(
    module.ccall('poc_load_disk', 'number', ['string'], ['/omega-test.dsk']),
    0,
    'the default Omega WD2793 must accept a disk image'
  );
  module._poc_eject_disk();

  assert.strictEqual(
    module._poc_init_model(1, 0),
    0,
    'RainBIOS NMS 8250 must initialize'
  );
  assert.strictEqual(module._poc_omega_unified_bank(), -1);
  assert.strictEqual(module._poc_flip_omega_unified_bank(), -1);
  assert.strictEqual(module._poc_frame_hz(), 50);
  assert.strictEqual(module._poc_has_floppy(), 1);
  assert.strictEqual(module._poc_ram_kb(), 128);
  assert.strictEqual(module._poc_set_ram_kb(512), 512);
  assert.strictEqual(module._poc_ram_kb(), 512);
  assert.strictEqual(module._poc_set_ram_kb(1024), 1024);
  assert.strictEqual(module._poc_ram_kb(), 1024);
  assert.strictEqual(module._poc_set_ram_kb(4096), 4096);
  assert.strictEqual(module._poc_ram_kb(), 4096);
  assert.strictEqual(module._poc_set_ram_kb(32), -1);
  assert.strictEqual(module._poc_ram_kb(), 4096);
  assert.strictEqual(module._poc_set_ram_kb(128), 128);
  for (let frame = 0; frame < 300; ++frame) module._poc_step();
  pixelCount = module._poc_width() * module._poc_height();
  pixelStart = module._poc_pixels() >>> 2;
  colors = new Set(
    module.HEAPU32.subarray(pixelStart, pixelStart + pixelCount)
  );
  assert(colors.size > 1, 'RainBIOS must render a non-blank boot display');
  module._poc_audio_reset();

  module.FS.writeFile('/test.dsk', new Uint8Array(737280));
  assert.strictEqual(
    module.ccall('poc_load_disk', 'number', ['string'], ['/test.dsk']),
    0,
    'the NMS 8250 WD2793 must accept a disk image'
  );
  module._poc_eject_disk();

  const cartridge = new Uint8Array(0x2000);
  cartridge.fill(0xff);
  module.FS.writeFile('/test-slot-1.rom', cartridge);
  module.FS.writeFile('/test-slot-2.rom', cartridge);
  assert.strictEqual(
    module.ccall(
      'poc_load_cartridge_slot', 'number', ['number', 'string'],
      [0, '/test-slot-1.rom']
    ),
    0
  );
  assert.strictEqual(
    module.ccall(
      'poc_load_cartridge_slot', 'number', ['number', 'string'],
      [1, '/test-slot-2.rom']
    ),
    0
  );
  assert.strictEqual(module._poc_cartridge_loaded(0), 1);
  assert.strictEqual(module._poc_cartridge_loaded(1), 1);

  assert.strictEqual(module._poc_set_unapi(1), 1);
  assert.strictEqual(module._poc_unapi_enabled(), 1);
  assert.strictEqual(module._poc_unapi_probe(), 0xab);
  assert.strictEqual(module._poc_unapi_guest_active(), 1);
  assert.strictEqual(
    module._poc_cartridge_loaded(0), 1,
    'port-mapped UNAPI must leave cartridge I available'
  );
  assert.strictEqual(
    module._poc_cartridge_loaded(1), 1,
    'port-mapped UNAPI must leave cartridge II available'
  );
  module._poc_reset();
  assert.strictEqual(module._poc_unapi_enabled(), 1);
  assert.strictEqual(module._poc_unapi_guest_active(), 0);
  assert.strictEqual(module._poc_set_unapi(0), 0);

  assert.strictEqual(module._poc_set_sunrise(1), 1);
  assert.strictEqual(module._poc_sunrise_enabled(), 1);
  assert.strictEqual(module._poc_sunrise_slot(), 0);
  assert.strictEqual(module._poc_cartridge_loaded(0), 0);
  assert.strictEqual(
    module._poc_cartridge_loaded(1), 1,
    'Sunrise alone must leave cartridge II available'
  );
  module.FS.writeFile('/test-ide.img', new Uint8Array(1024 * 1024));
  assert.strictEqual(
    module.ccall(
      'poc_mount_ide', 'number', ['string', 'number'],
      ['/test-ide.img', 1]
    ),
    0
  );
  assert.strictEqual(module._poc_ide_mounted(), 1);
  assert.strictEqual(module._poc_ide_writable(), 1);
  assert.strictEqual(module._poc_set_ram_kb(512), 512);
  assert.strictEqual(module._poc_sunrise_enabled(), 1);
  assert.strictEqual(module._poc_ide_mounted(), 1);
  module._poc_reset();
  assert.strictEqual(module._poc_ide_mounted(), 1);
  assert.strictEqual(module._poc_eject_ide(), 0);
  assert.strictEqual(module._poc_ide_mounted(), 0);

  assert.strictEqual(module._poc_set_sd_mapper(1), 1);
  assert.strictEqual(module._poc_sd_mapper_enabled(), 1);
  assert.strictEqual(module._poc_sd_mapper_slot(), 1);
  assert.strictEqual(module._poc_cartridge_loaded(0), 0);
  assert.strictEqual(module._poc_cartridge_loaded(1), 0);
  module.FS.writeFile('/test-sd.img', new Uint8Array(1024 * 1024));
  assert.strictEqual(
    module.ccall(
      'poc_mount_sd_card', 'number', ['number', 'string', 'number'],
      [0, '/test-sd.img', 1]
    ),
    0
  );
  assert.strictEqual(module._poc_sd_card_mounted(0), 1);
  module._poc_reset();
  assert.strictEqual(module._poc_sd_mapper_enabled(), 1);
  assert.strictEqual(module._poc_sd_card_mounted(0), 1);
  assert.strictEqual(module._poc_eject_sd_card(0), 0);
  assert.strictEqual(module._poc_sd_card_mounted(0), 0);
  assert.strictEqual(module._poc_set_sunrise(0), 0);
  assert.strictEqual(module._poc_sunrise_enabled(), 0);
  assert.strictEqual(
    module._poc_sd_mapper_slot(), 0,
    'SD Mapper must move back to cartridge I when Sunrise is disabled'
  );
  assert.strictEqual(module._poc_set_sd_mapper(0), 0);
  assert.strictEqual(module._poc_sd_mapper_enabled(), 0);

  const scsiRom = module.FS.readFile('roms/bertscsi-v2-30h-37h.rom');
  const alternateScsiRom = module.FS.readFile(
    'roms/bertscsi-v1-d0h-d7h.rom'
  );
  assert.strictEqual(scsiRom.byteLength, 128 * 1024);
  assert.strictEqual(alternateScsiRom.byteLength, 128 * 1024);
  assert.notDeepStrictEqual(
    scsiRom, alternateScsiRom,
    'the two embedded SCSI controller revisions must remain distinct'
  );
  const scsiRomPointer = module._malloc(scsiRom.byteLength);
  assert.notStrictEqual(scsiRomPointer, 0);
  try {
    module.HEAPU8.set(scsiRom, scsiRomPointer);
    assert.strictEqual(
      module._poc_install_scsi_rom(scsiRomPointer, scsiRom.byteLength - 1, 0),
      -1,
      'a non-banked SCSI controller ROM must be rejected'
    );
    assert.strictEqual(
      module._poc_install_scsi_rom(scsiRomPointer, scsiRom.byteLength, 0),
      0,
      'the bundled v2 SCSI controller ROM must be accepted'
    );
  } finally {
    module._free(scsiRomPointer);
  }
  assert.strictEqual(module._poc_scsi_rom_ready(), 1);
  assert.strictEqual(module._poc_set_scsi_target_id(4), 4);
  assert.strictEqual(module._poc_scsi_target_id(), 4);
  assert.strictEqual(module._poc_scsi_io_base(), 0x30);
  assert.strictEqual(module._poc_set_scsi_io_base(0xd0), 0xd0);
  assert.strictEqual(module._poc_scsi_io_base(), 0xd0);
  assert.strictEqual(module._poc_set_scsi_io_base(0x30), 0x30);
  assert.strictEqual(module._poc_scsi_io_base(), 0x30);
  assert.strictEqual(module._poc_set_scsi_io_base(0x80), -1);
  assert.strictEqual(module._poc_set_scsi(1), 1);
  assert.strictEqual(module._poc_scsi_enabled(), 1);
  assert.strictEqual(module._poc_scsi_slot(), 0);
  module.FS.writeFile('/test-scsi.img', new Uint8Array(1024 * 1024));
  assert.strictEqual(
    module.ccall(
      'poc_mount_scsi', 'number', ['string', 'number'],
      ['/test-scsi.img', 1]
    ),
    0
  );
  assert.strictEqual(module._poc_scsi_disk_mounted(), 1);
  assert.strictEqual(module._poc_scsi_disk_writable(), 1);
  module._poc_reset();
  assert.strictEqual(module._poc_scsi_disk_mounted(), 1);
  assert.strictEqual(module._poc_eject_scsi_disk(), 0);
  assert.strictEqual(module._poc_scsi_disk_mounted(), 0);
  assert.strictEqual(module._poc_set_sd_mapper(1), 1);
  assert.strictEqual(module._poc_scsi_slot(), 0);
  assert.strictEqual(module._poc_sd_mapper_slot(), 1);
  assert.strictEqual(
    module._poc_set_sunrise(1), -1,
    'SCSI and SD Mapper must consume both cartridge slots'
  );
  assert.strictEqual(module._poc_set_scsi(0), 0);
  assert.strictEqual(module._poc_sd_mapper_slot(), 0);
  assert.strictEqual(module._poc_set_sd_mapper(0), 0);

  const internalVdpPixels = module._poc_pixels();
  assert.strictEqual(module._poc_set_powergraph_v9990(1), 1);
  assert.strictEqual(module._poc_powergraph_v9990_enabled(), 1);
  assert.strictEqual(module._poc_powergraph_v9990_slot(), 0);
  assert.strictEqual(module._poc_powergraph_video_source(), 0);
  assert.strictEqual(module._poc_powergraph_output_active(), 0);
  assert.strictEqual(
    module._poc_pixels(), internalVdpPixels,
    'Auto must retain the MSX VDP until V9990 software enables its display'
  );
  assert.strictEqual(module._poc_set_powergraph_video_source(3), -1);
  assert.strictEqual(module._poc_set_powergraph_video_source(2), 2);
  assert.strictEqual(module._poc_powergraph_output_active(), 1);
  assert.notStrictEqual(
    module._poc_pixels(), internalVdpPixels,
    'the V9990 override must expose the PowerGraph framebuffer'
  );
  assert.strictEqual(module._poc_set_powergraph_video_source(1), 1);
  assert.strictEqual(module._poc_powergraph_output_active(), 0);
  assert.strictEqual(module._poc_pixels(), internalVdpPixels);
  assert.strictEqual(module._poc_set_powergraph_video_source(0), 0);
  assert.strictEqual(module._poc_set_sunrise(1), 1);
  assert.strictEqual(module._poc_sunrise_slot(), 0);
  assert.strictEqual(
    module._poc_powergraph_v9990_slot(), 1,
    'PowerGraph must move to cartridge II behind Sunrise'
  );
  assert.strictEqual(
    module._poc_set_sd_mapper(1), -1,
    'a third cartridge extension must be rejected'
  );
  assert.strictEqual(module._poc_sd_mapper_enabled(), 0);
  assert.strictEqual(module._poc_set_sunrise(0), 0);
  assert.strictEqual(module._poc_powergraph_v9990_slot(), 0);
  assert.strictEqual(module._poc_set_sd_mapper(1), 1);
  assert.strictEqual(module._poc_sd_mapper_slot(), 0);
  assert.strictEqual(module._poc_powergraph_v9990_slot(), 1);
  assert.strictEqual(module._poc_set_powergraph_v9990(0), 0);
  assert.strictEqual(module._poc_powergraph_v9990_enabled(), 0);
  assert.strictEqual(module._poc_pixels(), internalVdpPixels);
  assert.strictEqual(module._poc_set_sd_mapper(0), 0);

  assert.strictEqual(module._poc_set_input_device(0), 0);
  module._poc_joy(4, 1);
  assert.strictEqual(module._poc_joy_matrix() & 0x10, 0);
  module._poc_joy(4, 0);
  assert.strictEqual(module._poc_joy_matrix() & 0x3f, 0x3f);

  assert.strictEqual(module._poc_set_input_device(1), 0);
  module._poc_mouse_motion(12, -7);
  module._poc_mouse_button(0, 1);
  module._poc_step();
  module._poc_mouse_button(0, 0);
  module._poc_mouse_clear();
  assert.strictEqual(module._poc_set_input_device(0), 0);
  assert.strictEqual(module._poc_set_input_device(2), -1);

  assert.strictEqual(module._poc_set_sunrise(1), 1);
  assert.strictEqual(module._poc_set_sd_mapper(1), 1);
  assert.strictEqual(module._poc_set_powergraph_v9990(1), -1);
  assert.strictEqual(module._poc_set_unapi(1), 1);
  assert.strictEqual(module._poc_init_model(0, 0), 0);
  assert.strictEqual(module._poc_frame_hz(), 60);
  assert.strictEqual(module._poc_has_floppy(), 0);
  assert.strictEqual(module._poc_cartridge_loaded(0), 0);
  assert.strictEqual(module._poc_cartridge_loaded(1), 0);
  assert.strictEqual(module._poc_sd_mapper_enabled(), 1);
  assert.strictEqual(module._poc_sunrise_enabled(), 1);
  assert.strictEqual(module._poc_sunrise_slot(), 0);
  assert.strictEqual(module._poc_sd_mapper_slot(), 1);
  assert.strictEqual(module._poc_powergraph_v9990_enabled(), 0);
  assert.strictEqual(module._poc_powergraph_video_source(), 0);
  assert.strictEqual(module._poc_unapi_enabled(), 1);
  assert.strictEqual(module._poc_set_sunrise(0), 0);
  assert.strictEqual(module._poc_set_sd_mapper(0), 0);
  assert.strictEqual(module._poc_set_unapi(0), 0);

  console.log('WASM machine profile and peripheral smoke tests passed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
