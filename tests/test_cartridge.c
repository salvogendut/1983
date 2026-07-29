#include "cartridge.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 *banked_rom(size_t bank_size, size_t banks) {
    u8 *rom = malloc(bank_size * banks);

    assert(rom);
    for (size_t bank = 0; bank < banks; ++bank)
        memset(rom + bank * bank_size, (int)bank, bank_size);
    rom[0] = 'A';
    rom[1] = 'B';
    return rom;
}

static void test_mapper_names_and_detection(void) {
    MsxCartridgeMapper mapper = MSX_CART_MAPPER_AUTO;
    u8 *rom = calloc(1, 0x10000);

    assert(rom);
    assert(strcmp(msx_cartridge_mapper_name(MSX_CART_MAPPER_KONAMI_SCC),
                  "konami-scc") == 0);
    assert(msx_cartridge_mapper_from_name("ASCII16", &mapper));
    assert(mapper == MSX_CART_MAPPER_ASCII16);
    assert(msx_cartridge_mapper_from_name("konami-scc", &mapper));
    assert(mapper == MSX_CART_MAPPER_KONAMI_SCC);
    assert(!msx_cartridge_mapper_from_name("guess", &mapper));
    assert(!msx_cartridge_mapper_from_name(NULL, &mapper));

    assert(msx_cartridge_detect_mapper(rom, 0x8000) ==
           MSX_CART_MAPPER_LINEAR);
    assert(msx_cartridge_detect_mapper(rom, 0x10000) ==
           MSX_CART_MAPPER_LINEAR);
    rom[0] = 'A';
    rom[1] = 'B';
    rom[0x100] = 0x32;
    rom[0x101] = 0x00;
    rom[0x102] = 0x68;
    rom[0x110] = 0x32;
    rom[0x111] = 0x00;
    rom[0x112] = 0x78;
    assert(msx_cartridge_detect_mapper(rom, 0x10000) ==
           MSX_CART_MAPPER_ASCII8);
    rom[0x120] = 0x32;
    rom[0x121] = 0xff;
    rom[0x122] = 0x77;
    rom[0x130] = 0x32;
    rom[0x131] = 0xff;
    rom[0x132] = 0x77;
    assert(msx_cartridge_detect_mapper(rom, 0x10000) ==
           MSX_CART_MAPPER_ASCII16);
    memset(rom, 0, 0x10000);
    rom[0] = 'A';
    rom[1] = 'B';
    rom[0x100] = 0x32;
    rom[0x101] = 0x00;
    rom[0x102] = 0x90;
    assert(msx_cartridge_detect_mapper(rom, 0x10000) ==
           MSX_CART_MAPPER_KONAMI_SCC);
    free(rom);
}

static void test_linear_mapping(void) {
    MsxCartridge cartridge;
    u8 rom[0x4000];

    msx_cartridge_init(&cartridge);
    memset(rom, 0x5a, sizeof(rom));
    rom[0] = 'A';
    rom[1] = 'B';
    rom[2] = 0x10;
    rom[3] = 0x40;
    assert(msx_cartridge_install(&cartridge, rom, sizeof(rom),
                                 MSX_CART_MAPPER_AUTO) == 0);
    assert(cartridge.mapper == MSX_CART_MAPPER_LINEAR);
    assert(msx_cartridge_read(&cartridge, 0x3fff) == 0xff);
    assert(msx_cartridge_read(&cartridge, 0x4000) == 'A');
    assert(msx_cartridge_read(&cartridge, 0x7fff) == 0x5a);
    assert(msx_cartridge_read(&cartridge, 0x8000) == 0xff);

    rom[2] = 0x00;
    rom[3] = 0x80;
    assert(msx_cartridge_install(&cartridge, rom, sizeof(rom),
                                 MSX_CART_MAPPER_LINEAR) == 0);
    assert(msx_cartridge_read(&cartridge, 0x4000) == 0xff);
    assert(msx_cartridge_read(&cartridge, 0x8000) == 'A');
    msx_cartridge_write(&cartridge, 0x8000, 7);
    assert(msx_cartridge_read(&cartridge, 0x8000) == 'A');

    assert(msx_cartridge_install(&cartridge, rom, sizeof(rom),
                                 MSX_CART_MAPPER_COUNT) < 0);
    assert(cartridge.loaded);
    msx_cartridge_eject(&cartridge);
    assert(!cartridge.loaded);
    assert(msx_cartridge_read(&cartridge, 0x8000) == 0xff);
}

static void test_ascii8_mapping(void) {
    MsxCartridge cartridge;
    u8 *rom = banked_rom(0x2000, 6);

    msx_cartridge_init(&cartridge);
    assert(msx_cartridge_install(&cartridge, rom, 0x2000 * 6,
                                 MSX_CART_MAPPER_ASCII8) == 0);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 0);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 0);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 0);
    assert(msx_cartridge_read(&cartridge, 0xa004) == 0);
    msx_cartridge_write(&cartridge, 0x6000, 1);
    msx_cartridge_write(&cartridge, 0x6800, 2);
    msx_cartridge_write(&cartridge, 0x7000, 3);
    msx_cartridge_write(&cartridge, 0x7800, 4);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 1);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 2);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 3);
    assert(msx_cartridge_read(&cartridge, 0xa004) == 4);
    assert(msx_cartridge_read(&cartridge, 0xc000) == 0xff);
    msx_cartridge_write(&cartridge, 0x6000, 6);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 0xff);
    msx_cartridge_write(&cartridge, 0x6000, 8);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 0);
    msx_cartridge_reset(&cartridge);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 0);
    msx_cartridge_destroy(&cartridge);
    free(rom);
}

static void test_ascii16_mapping(void) {
    MsxCartridge cartridge;
    u8 *rom = banked_rom(0x4000, 4);

    msx_cartridge_init(&cartridge);
    assert(msx_cartridge_install(&cartridge, rom, 0x4000 * 4,
                                 MSX_CART_MAPPER_ASCII16) == 0);
    msx_cartridge_write(&cartridge, 0x6000, 2);
    msx_cartridge_write(&cartridge, 0x7000, 3);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 2);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 3);
    msx_cartridge_write(&cartridge, 0x6800, 1);
    msx_cartridge_write(&cartridge, 0x7800, 1);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 2);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 3);
    msx_cartridge_reset(&cartridge);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 0);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 0);
    msx_cartridge_destroy(&cartridge);
    free(rom);
}

static void test_konami_mapping(void) {
    MsxCartridge cartridge;
    u8 *rom = banked_rom(0x2000, 8);

    msx_cartridge_init(&cartridge);
    assert(msx_cartridge_install(&cartridge, rom, 0x2000 * 8,
                                 MSX_CART_MAPPER_KONAMI) == 0);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 0);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 1);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 2);
    assert(msx_cartridge_read(&cartridge, 0xa004) == 3);
    assert(msx_cartridge_read(&cartridge, 0x0004) == 0);
    assert(msx_cartridge_read(&cartridge, 0xc004) == 2);
    msx_cartridge_write(&cartridge, 0x6000, 5);
    msx_cartridge_write(&cartridge, 0x8000, 6);
    msx_cartridge_write(&cartridge, 0xa000, 7);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 5);
    assert(msx_cartridge_read(&cartridge, 0x8004) == 6);
    assert(msx_cartridge_read(&cartridge, 0xa004) == 7);
    assert(msx_cartridge_read(&cartridge, 0xc004) == 6);
    msx_cartridge_destroy(&cartridge);
    free(rom);
}

static void test_konami_scc_mapping(void) {
    MsxCartridge cartridge;
    u8 *rom = banked_rom(0x2000, 16);

    msx_cartridge_init(&cartridge);
    assert(msx_cartridge_install(&cartridge, rom, 0x2000 * 16,
                                 MSX_CART_MAPPER_KONAMI_SCC) == 0);
    assert(msx_cartridge_read(&cartridge, 0x0004) == 2);
    assert(msx_cartridge_read(&cartridge, 0xc004) == 0);
    msx_cartridge_write(&cartridge, 0x5000, 4);
    msx_cartridge_write(&cartridge, 0x7000, 5);
    msx_cartridge_write(&cartridge, 0x9000, 0x3f);
    msx_cartridge_write(&cartridge, 0xb000, 7);
    assert(msx_cartridge_read(&cartridge, 0x4004) == 4);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 5);
    assert(msx_cartridge_read(&cartridge, 0x9004) == 0x0f);
    assert(msx_cartridge_read(&cartridge, 0xa004) == 7);
    assert(cartridge.scc_enabled);
    msx_cartridge_write(&cartridge, 0x98a5, 0x66);
    assert(msx_cartridge_read(&cartridge, 0x98a5) == 0x66);
    assert(msx_cartridge_read(&cartridge, 0x99a5) == 0x66);
    msx_cartridge_write(&cartridge, 0x9000, 2);
    assert(!cartridge.scc_enabled);
    assert(msx_cartridge_read(&cartridge, 0x9804) == 2);
    msx_cartridge_reset(&cartridge);
    assert(!cartridge.scc_enabled);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 1);
    assert(msx_cartridge_set_mapper(&cartridge,
                                    MSX_CART_MAPPER_ASCII8) == 0);
    assert(cartridge.mapper == MSX_CART_MAPPER_ASCII8);
    assert(msx_cartridge_read(&cartridge, 0x6004) == 0);
    msx_cartridge_destroy(&cartridge);
    free(rom);
}

static void test_size_validation(void) {
    MsxCartridge cartridge;
    u8 *rom = calloc(1, 0x10001);

    assert(rom);
    msx_cartridge_init(&cartridge);
    assert(msx_cartridge_install(&cartridge, rom, 0x10001,
                                 MSX_CART_MAPPER_LINEAR) < 0);
    assert(!cartridge.loaded);
    msx_cartridge_destroy(&cartridge);
    free(rom);
}

int main(void) {
    test_mapper_names_and_detection();
    test_linear_mapping();
    test_ascii8_mapping();
    test_ascii16_mapping();
    test_konami_mapping();
    test_konami_scc_mapping();
    test_size_validation();
    puts("cartridge mapper tests passed");
    return 0;
}
