#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *path = "test-config-roundtrip.tmp";
    Config config;
    Config loaded;

    config_defaults(&config);
    assert(!config.cartridge_path[0][0]);
    assert(!config.cartridge_path[1][0]);
    assert(config.cartridge_mapper[0] == MSX_CART_MAPPER_AUTO);
    assert(config.cartridge_mapper[1] == MSX_CART_MAPPER_AUTO);

    snprintf(config.path, sizeof(config.path), "%s", path);
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
