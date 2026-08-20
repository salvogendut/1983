#include "sd_mapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    static const char *card_path = "test-sd-mapper-card.tmp";
    MsxSdMapper mapper;
    u8 *rom = malloc(MSX_SD_MAPPER_ROM_SIZE);
    FILE *file;

    assert(rom);
    for (unsigned bank = 0;
         bank < MSX_SD_MAPPER_ROM_SIZE /
                    MSX_SD_MAPPER_ROM_BANK_SIZE;
         ++bank) {
        memset(rom + bank * MSX_SD_MAPPER_ROM_BANK_SIZE,
               bank, MSX_SD_MAPPER_ROM_BANK_SIZE);
    }

    sd_mapper_init(&mapper);
    assert(sd_mapper_install_rom(
               &mapper, rom, MSX_SD_MAPPER_ROM_SIZE) == 0);
    assert(sd_mapper_slot_expanded(&mapper));
    assert(sd_mapper_secondary_read(&mapper) == 0xff);
    assert(sd_mapper_read(&mapper, 0x4000) == 0);
    assert(sd_mapper_read(&mapper, 0x8000) == 0xff);

    sd_mapper_write(&mapper, 0x6000, 6);
    assert(sd_mapper_read(&mapper, 0x4000) == 6);
    sd_mapper_write(&mapper, 0x7000, 0x0b);
    assert(sd_mapper_read(&mapper, 0x8000) == 3);
    sd_mapper_set_alternate_driver(&mapper, true);
    assert(sd_mapper_read(&mapper, 0x4000) == 14);
    assert(sd_mapper_read(&mapper, 0x8000) == 11);

    sd_mapper_secondary_write(&mapper, 0x55);
    assert(sd_mapper_secondary_read(&mapper) == 0xaa);
    assert(sd_mapper_selected_subslot(&mapper, 0x0000) == 1);
    assert(sd_mapper_selected_subslot(&mapper, 0x4000) == 1);
    assert(sd_mapper_selected_subslot(&mapper, 0x8000) == 1);
    assert(sd_mapper_selected_subslot(&mapper, 0xc000) == 1);
    sd_mapper_write(&mapper, 0x4000, 0x83);
    assert(sd_mapper_read(&mapper, 0x4000) == 0x83);
    assert(sd_mapper_io_read(&mapper, 0) == 0xe3);
    sd_mapper_io_write(&mapper, 0, 31);
    assert(sd_mapper_io_read(&mapper, 0) == 0xff);
    sd_mapper_write(&mapper, 0x4000, 0x19);
    assert(sd_mapper_read(&mapper, 0x4000) == 0x19);

    /* SymbOS can address the mounted cards through its MegaSD driver. */
    sd_mapper_set_alternate_driver(&mapper, false);
    sd_mapper_secondary_write(&mapper, 0);
    sd_mapper_write(&mapper, 0x6000, 0x40);
    sd_mapper_write(&mapper, 0x5800, 1);
    sd_mapper_write(&mapper, 0x4000, 0xff);
    assert(mapper.mega_sd_compat);
    assert(mapper.mega_sd_selected_card == 1);
    assert(!mapper.cards[0].selected);
    assert(mapper.cards[1].selected);
    assert(sd_mapper_read(&mapper, 0x5000) == 0xff);
    assert(!mapper.cards[1].selected);
    sd_mapper_write(&mapper, 0x5800, 0);
    sd_mapper_write(&mapper, 0x4000, 0xff);
    assert(mapper.cards[0].selected);
    sd_mapper_write(&mapper, 0x6000, 0);
    assert(!mapper.mega_sd_compat);
    assert(!mapper.cards[0].selected);

    sd_mapper_set_alternate_driver(&mapper, true);
    sd_mapper_secondary_write(&mapper, 0);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x06);

    file = fopen(card_path, "wb");
    assert(file);
    for (unsigned i = 0; i < 2 * SD_CARD_SECTOR_SIZE; ++i)
        assert(fputc(0, file) != EOF);
    assert(fclose(file) == 0);
    assert(sd_mapper_mount_card(
               &mapper, 0, card_path, SD_IMAGE_READ_ONLY) == 0);
    assert(sd_mapper_mount_card(
               &mapper, 1, card_path, SD_IMAGE_READ_WRITE) == 0);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x05);
    sd_mapper_write(&mapper, 0x7ff0, 2);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x01);
    sd_mapper_write(&mapper, 0x7ff0, 1);
    assert(sd_mapper_read(&mapper, 0x7ff0) == 0x04);

    sd_mapper_write(&mapper, 0x7ff1, 2);
    assert(sd_mapper_read(&mapper, 0x7ff1) == 2);
    sd_mapper_tick(&mapper, 37, 3579545);
    assert(sd_mapper_read(&mapper, 0x7ff1) < 2);

    sd_mapper_reset(&mapper);
    assert(sd_mapper_io_read(&mapper, 0) == 0xe3);
    assert(sd_mapper_io_read(&mapper, 1) == 0xe2);
    assert(sd_mapper_io_read(&mapper, 2) == 0xe1);
    assert(sd_mapper_io_read(&mapper, 3) == 0xe0);
    assert(sd_mapper_secondary_read(&mapper) == 0xff);

    sd_mapper_set_mapper_enabled(&mapper, false);
    assert(!sd_mapper_slot_expanded(&mapper));
    assert(sd_mapper_io_read(&mapper, 0) == 0xff);
    assert(sd_mapper_read(&mapper, 0x4000) == 8);

    assert(sd_mapper_eject_rom(&mapper) == 0);
    assert(!sd_mapper_card_mounted(&mapper, 0));
    assert(!sd_mapper_card_mounted(&mapper, 1));
    sd_mapper_destroy(&mapper);
    assert(remove(card_path) == 0);
    free(rom);
    return 0;
}
