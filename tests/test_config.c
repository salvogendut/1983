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
    assert(!config.cartridge_path[0][0]);
    assert(!config.cartridge_path[1][0]);
    assert(config.cartridge_mapper[0] == MSX_CART_MAPPER_AUTO);
    assert(config.cartridge_mapper[1] == MSX_CART_MAPPER_AUTO);

    snprintf(config.path, sizeof(config.path), "%s", path);
    config.model = MSX_MODEL_PHILIPS_NMS8250;
    snprintf(config.machine_id, sizeof(config.machine_id),
             "my-nms8250");
    config.memory_kb = 128;
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
    snprintf(config.last_media_dir, sizeof(config.last_media_dir),
             "/roms");
    assert(config_save(&config) == 0);

    config_load(&loaded, path);
    assert(loaded.model == MSX_MODEL_PHILIPS_NMS8250);
    assert(strcmp(loaded.machine_id, "my-nms8250") == 0);
    assert(loaded.memory_kb == 128);
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
    assert(strcmp(loaded.last_media_dir, "/roms") == 0);
    assert(remove(path) == 0);

    puts("configuration media tests passed");
    return 0;
}
