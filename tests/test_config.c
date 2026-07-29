#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *path = "test-config-roundtrip.tmp";
    Config config;
    Config loaded;

    config_defaults(&config);
    assert(strcmp(config.machine_id, "msx1") == 0);
    assert(!config.bios_path[0]);
    assert(!config.logo_path[0]);
    assert(!config.subrom_path[0]);
    assert(!config.disk_rom_path[0]);
    assert(!config.sunrise_rom_path[0]);
    assert(!config.ide_image_path[0]);
    assert(!config.cassette_path[0]);
    assert(!config.cartridge_path[0][0]);
    assert(!config.cartridge_path[1][0]);
    assert(config.cartridge_mapper[0] == MSX_CART_MAPPER_AUTO);
    assert(config.cartridge_mapper[1] == MSX_CART_MAPPER_AUTO);
    assert(config.main_input == INPUT_PORT_A);
    assert(config.joy_port_device[0] == JOY_PORT_JOYSTICK);
    assert(config.joy_port_device[1] == JOY_PORT_JOYSTICK);
    assert(!config.extra_hardware);
    assert(config_cartridge_extension_count(&config) == 0);
    assert(config_cartridge_slot_available(&config, 0));
    assert(config_cartridge_slot_available(&config, 1));

    snprintf(config.path, sizeof(config.path), "%s", path);
    config.model = MSX_MODEL_PHILIPS_NMS8250;
    snprintf(config.machine_id, sizeof(config.machine_id),
             "my-nms8250");
    config.memory_kb = 4096;
    snprintf(config.bios_path, sizeof(config.bios_path),
             "/roms/nms8250_basic-bios2.rom");
    snprintf(config.logo_path, sizeof(config.logo_path),
             "/roms/cbios_logo.rom");
    snprintf(config.subrom_path, sizeof(config.subrom_path),
             "/roms/nms8250_msx2sub.rom");
    snprintf(config.disk_rom_path, sizeof(config.disk_rom_path),
             "/roms/nms8250_disk.rom");
    snprintf(config.cartridge_path[0],
             sizeof(config.cartridge_path[0]),
             "/roms/Metal Gear.rom");
    snprintf(config.cartridge_path[1],
             sizeof(config.cartridge_path[1]),
             "/roms/Arkanoid.rom");
    config.cartridge_mapper[0] = MSX_CART_MAPPER_KONAMI_SCC;
    config.cartridge_mapper[1] = MSX_CART_MAPPER_ASCII8;
    config.main_input = INPUT_PORT_B;
    config.joy_port_device[0] = JOY_PORT_MOUSE;
    config.extra_hardware = true;
    config.sunrise_ide = true;
    snprintf(config.sunrise_rom_path,
             sizeof(config.sunrise_rom_path),
             "/roms/Nextor-2.1.1.SunriseIDE.ROM");
    snprintf(config.ide_image_path,
             sizeof(config.ide_image_path),
             "/disks/GBMSX.IMG");
    snprintf(config.cassette_path,
             sizeof(config.cassette_path),
             "/tapes/software.cas");
    config.scc = true;
    assert(config_cartridge_extension_count(&config) == 2);
    assert(strcmp(config_cartridge_slot_owner(&config, 0),
                  "Konami SCC") == 0);
    assert(strcmp(config_cartridge_slot_owner(&config, 1),
                  "Sunrise IDE") == 0);
    assert(!config_cartridge_slot_available(&config, 0));
    assert(!config_cartridge_slot_available(&config, 1));
    snprintf(config.last_media_dir, sizeof(config.last_media_dir),
             "/roms");
    assert(config_save(&config) == 0);

    config_load(&loaded, path);
    assert(loaded.model == MSX_MODEL_PHILIPS_NMS8250);
    assert(strcmp(loaded.machine_id, "my-nms8250") == 0);
    assert(loaded.memory_kb == 4096);
    assert(loaded.extra_hardware);
    assert(loaded.sunrise_ide);
    assert(strcmp(loaded.sunrise_rom_path,
                  "/roms/Nextor-2.1.1.SunriseIDE.ROM") == 0);
    assert(strcmp(loaded.ide_image_path,
                  "/disks/GBMSX.IMG") == 0);
    assert(strcmp(loaded.cassette_path,
                  "/tapes/software.cas") == 0);
    assert(loaded.scc);
    assert(strcmp(loaded.bios_path,
                  "/roms/nms8250_basic-bios2.rom") == 0);
    assert(strcmp(loaded.logo_path, "/roms/cbios_logo.rom") == 0);
    assert(strcmp(loaded.subrom_path,
                  "/roms/nms8250_msx2sub.rom") == 0);
    assert(strcmp(loaded.disk_rom_path,
                  "/roms/nms8250_disk.rom") == 0);
    assert(strcmp(loaded.cartridge_path[0],
                  "/roms/Metal Gear.rom") == 0);
    assert(strcmp(loaded.cartridge_path[1],
                  "/roms/Arkanoid.rom") == 0);
    assert(loaded.cartridge_mapper[0] ==
           MSX_CART_MAPPER_KONAMI_SCC);
    assert(loaded.cartridge_mapper[1] ==
           MSX_CART_MAPPER_ASCII8);
    assert(loaded.main_input == INPUT_PORT_B);
    assert(loaded.joy_port_device[0] == JOY_PORT_MOUSE);
    assert(loaded.joy_port_device[1] == JOY_PORT_JOYSTICK);
    assert(strcmp(loaded.last_media_dir, "/roms") == 0);

    loaded.msx_music = true;
    config_normalize(&loaded);
    assert(loaded.sunrise_ide);
    assert(loaded.scc);
    assert(!loaded.msx_music);
    loaded.sunrise_ide = false;
    config_normalize(&loaded);
    assert(config_cartridge_slot_available(&loaded, 0));
    assert(strcmp(config_cartridge_slot_owner(&loaded, 1),
                  "Konami SCC") == 0);
    loaded.main_input = (InputPort)99;
    loaded.joy_port_device[0] = (JoyPortDevice)99;
    config_normalize(&loaded);
    assert(loaded.main_input == INPUT_PORT_A);
    assert(loaded.joy_port_device[0] == JOY_PORT_JOYSTICK);
    assert(remove(path) == 0);

    puts("configuration media tests passed");
    return 0;
}
